# -*- coding: utf-8 -*-
"""Memoria OS 模拟器 CHAT 远程代理：转发到真服务器 + CORS 头。
用途：模拟器是 file:// 打开，直接 fetch 服务器会因无 CORS 头被浏览器拦，
本代理加 Access-Control-Allow-Origin: * 打通。
用法：python chat_proxy.py [端口=8333]
"""
import http.server
import urllib.request
import sys

UPSTREAM = 'http://ser270472228666.ahostxg.idc001.site'
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8333


class H(http.server.BaseHTTPRequestHandler):
    def _relay(self, body=None, ctype_in=None):
        try:
            u = UPSTREAM + self.path
            headers = {'User-Agent': 'MemoriaSim/1.0'}
            if ctype_in:
                headers['Content-Type'] = ctype_in
            if body is not None:
                req = urllib.request.Request(u, data=body, headers=headers, method=self.command)
            else:
                req = urllib.request.Request(u, headers=headers)
            with urllib.request.urlopen(req, timeout=15) as r:
                data = r.read()
                ctype = r.headers.get('Content-Type', 'application/octet-stream')
            self.send_response(200)
            self.send_header('Content-Type', ctype)
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:
            self.send_response(502)
            self.send_header('Content-Type', 'text/plain; charset=utf-8')
            self.send_header('Access-Control-Allow-Origin', '*')
            body = ('proxy err: %s' % e).encode('utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    def do_GET(self):
        self._relay()

    def do_POST(self):
        ln = int(self.headers.get('Content-Length', 0) or 0)
        body = self.rfile.read(ln) if ln > 0 else b''
        self._relay(body, self.headers.get('Content-Type', 'application/octet-stream'))

    def log_message(self, *a):
        pass


if __name__ == '__main__':
    http.server.HTTPServer(('127.0.0.1', PORT), H).serve_forever()
