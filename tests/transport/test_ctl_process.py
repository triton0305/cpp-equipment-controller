"""Real CLI/runtime processes, Unix sockets and a simulated STM32 PTY only."""
import os
import pathlib
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time

runtime_binary, cli_binary = sys.argv[1:]


def wait_for(predicate, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError('condition timed out')


with tempfile.TemporaryDirectory(prefix='ctl-process-') as directory:
    path = directory + '/control.sock'
    env = dict(os.environ, EQUIPMENT_CONTROL_SOCKET=path)
    missing = subprocess.run([cli_binary], env=env, input='', text=True,
                             capture_output=True, timeout=3)
    assert missing.returncode != 0 and 'not running' in missing.stderr

    # Mock endpoint verifies CLI parsing, exact forwarding and local help/exit.
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(path)
    listener.listen(1)
    cli = subprocess.Popen([cli_binary], env=env, stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    peer, _ = listener.accept()
    peer.settimeout(3)
    received = []

    def reply():
        with peer.makefile('rb') as stream:
            for line in stream:
                received.append(line)
                peer.sendall(b'OK\n')

    responder = threading.Thread(target=reply)
    responder.start()
    out, err = cli.communicate('help\ninvalid\nStArT\r\nstop\nSTOP\nStOp\r\nreset\nstatus\nexit\n', timeout=5)
    responder.join(3)
    peer.close()
    listener.close()
    os.unlink(path)
    assert cli.returncode == 0, err
    assert received == [b'start\n', b'reset\n', b'status\n'], received
    assert 'ctl> start reset status help exit\n' in out
    assert 'stop' not in out.lower()
    assert out.count('ERROR: invalid command; use help') == 4

    master, slave = os.openpty()
    os.set_blocking(master, False)
    done = threading.Event()
    estop = threading.Event()

    def sensors():
        while not done.wait(0.05):
            packet = ('DOOR:CLOSED\nESTOP:' + ('ON' if estop.is_set() else 'OFF') +
                      '\nPRESSURE:1500\nTEMP:25\nHUMIDITY:45\n').encode()
            try:
                os.write(master, packet)
                while os.read(master, 4096):
                    pass
            except BlockingIOError:
                pass

    feeder = threading.Thread(target=sensors)
    feeder.start()
    runtime = subprocess.Popen([runtime_binary, os.ttyname(slave)], env=env,
                               stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, text=True)
    connection = None
    idle_cli = None
    try:
        wait_for(lambda: pathlib.Path(path).exists())
        connection = socket.socket(socket.AF_UNIX)
        connection.settimeout(2)
        connection.connect(path)
        stream = connection.makefile('rb')

        def request(command):
            connection.sendall(command.encode() + b'\n')
            return stream.readline().decode().strip()

        wait_for(lambda: 'Ready=1' in request('status'))
        assert request('start') == 'OK'
        assert 'State=Run' in request('status')
        # CLI STOP must be rejected locally and leave an active process running.
        stream.close()
        connection.close()
        connection = None
        time.sleep(0.1)
        rejected = subprocess.run([cli_binary], env=env, input='stop\nstatus\nexit\n',
                                  text=True, capture_output=True, timeout=3)
        assert rejected.returncode == 0, rejected.stderr
        assert 'ERROR: invalid command; use help' in rejected.stdout
        assert 'State=Run' in rejected.stdout
        assert runtime.poll() is None
        time.sleep(0.1)
        connection = socket.socket(socket.AF_UNIX)
        connection.settimeout(2)
        connection.connect(path)
        stream = connection.makefile('rb')
        assert 'State=Run' in request('status')
        # The existing runtime stdin STOP path remains supported.
        runtime.stdin.write('stop\n')
        runtime.stdin.flush()
        wait_for(lambda: 'State=Idle' in request('status'))
        estop.set()
        wait_for(lambda: 'State=Error' in request('status'))
        assert request('reset').startswith('REJECTED')
        estop.clear()
        wait_for(lambda: 'EStop=OFF' in request('status'))
        assert request('reset') == 'OK'
        stream.close()
        connection.close()
        connection = None
        time.sleep(0.1)
        assert runtime.poll() is None
        exited = subprocess.run([cli_binary], env=env, input='status\nexit\n',
                                text=True, capture_output=True, timeout=3)
        assert exited.returncode == 0 and 'State=Idle' in exited.stdout
        assert runtime.poll() is None
        time.sleep(0.1)
        # CLI must detect runtime loss even while waiting for keyboard input.
        idle_cli = subprocess.Popen([cli_binary], env=env, stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        wait_for(lambda: bool(select.select([idle_cli.stdout], [], [], 0)[0]))
        runtime.terminate()
        runtime.wait(timeout=3)
        idle_cli.wait(timeout=3)
        assert idle_cli.returncode != 0
        assert b'connection closed' in idle_cli.stderr.read()
        assert not pathlib.Path(path).exists()
    finally:
        if connection is not None:
            connection.close()
        if idle_cli is not None and idle_cli.poll() is None:
            idle_cli.kill()
            idle_cli.wait()
        if runtime.poll() is None:
            runtime.terminate()
            runtime.wait(timeout=3)
        done.set()
        feeder.join(3)
        os.close(master)
        os.close(slave)
print('CLI process and PTY runtime checks passed')
