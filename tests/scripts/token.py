import json, os, socket, sys
s = socket.socket(socket.AF_UNIX)
s.connect(os.environ["SSHLX_SCRIPT_SOCKET"])
s.sendall(b'{"id":1,"method":"tabs","params":{}}\n')
sys.exit(0 if s.recv(100) == b"" else 3)
