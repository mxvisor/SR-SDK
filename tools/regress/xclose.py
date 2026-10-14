#!/usr/bin/env python3
"""Close the windows of the display $DISPLAY the way a window manager does: send
WM_DELETE_WINDOW to every top-level window (xdotool windowquit needs a window
manager, and Xvfb has none). Prints the number of windows it sent to. Xlib
through ctypes only."""
import ctypes, ctypes.util, sys

x11 = ctypes.CDLL(ctypes.util.find_library('X11'))
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XInternAtom.restype = ctypes.c_ulong
x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
x11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                           ctypes.POINTER(ctypes.c_uint)]
x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.c_void_p]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]


class ClientMessage(ctypes.Structure):
    _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int),
                ('display', ctypes.c_void_p), ('window', ctypes.c_ulong),
                ('message_type', ctypes.c_ulong), ('format', ctypes.c_int),
                ('data', ctypes.c_long * 5)]


def main():
    dpy = x11.XOpenDisplay(None)
    if not dpy:
        print('no display', file=sys.stderr)
        return 1
    root = x11.XDefaultRootWindow(dpy)
    protocols = x11.XInternAtom(dpy, b'WM_PROTOCOLS', 0)
    delete = x11.XInternAtom(dpy, b'WM_DELETE_WINDOW', 0)
    r, p = ctypes.c_ulong(), ctypes.c_ulong()
    kids, n = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
    x11.XQueryTree(dpy, root, ctypes.byref(r), ctypes.byref(p), ctypes.byref(kids), ctypes.byref(n))
    sent = 0
    for i in range(n.value):
        ev = ClientMessage()
        ev.type = 33            # ClientMessage
        ev.window = kids[i]
        ev.message_type = protocols
        ev.format = 32
        ev.data[0] = delete
        ev.data[1] = 0          # CurrentTime
        x11.XSendEvent(dpy, kids[i], 0, 0, ctypes.byref(ev))
        sent += 1
    x11.XSync(dpy, 0)
    print(sent)
    return 0


sys.exit(main())
