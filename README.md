# HTTP Server in C

This attempts to be a clone of the
[http.server](https://docs.python.org/3/library/http.server.html) Python
module. In this project I want to learn how the HTTP protocol works, how to
parse the requests and how to build responses that the browser/curl command can
understand.

### Quickstart

```console
gcc main.c -o main
./main
```

### Plans

I want to add the following features to the project
- arguments for the directory to use and port to serve the app to
- directory listings (Frontend in C)
- being able to load different types of files (like pdf and whatnot)
- 404 page for not found paths
- more error checks to make the app a bit friendlier when a user uses it like a dummy dumb (like me for example)
