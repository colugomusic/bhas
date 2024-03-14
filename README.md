This is a c++ wrapper around PortAudio which encapsulates all the awkwardness of cleanly stopping and starting audio streams into as simple an interface as I can manage.

If you build this using the provided CMakeLists then it will pull in my forked version of PortAudio which enables WASAPI Loopback support. If you try to use your own version of PortAudio then you'll need to make some trivial changes.

This library assumes you only ever want one audio stream running at a time, and always with exactly two output channels.
