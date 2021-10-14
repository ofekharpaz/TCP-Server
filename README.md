# TCP-Server
a TCP server in linux that listens to requests from localhost in a given port, and returns the file/folder in the specified path

The goal of this code is to simulate a server, when you get a request a thread is created to handle it by executing the
command. You can send a request by the browser or with telnet from the terminal.

After a request is sent the program parses it and find if its valid, if it is then return the requested path file/folder. if it isn't valid, we send an error message accoarding to the errors of HTTP.

The server supports HTTP/1.0 or HTTP/1.1.

To run the program, run the server.c file from terminal and insert 3 arguments: port, num of threads and
how many requests it should handle before self destructing. needless to say that those parameters need
to be positive.
