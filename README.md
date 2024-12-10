This is a c++ wrapper around PortAudio which encapsulates all the awkwardness of cleanly stopping and starting audio streams into as simple an interface as I can manage.

This library assumes you only ever want at most one audio stream running at a time, and always with exactly two output channels.

There is basic documentation [in the header](include/bhas.h).
