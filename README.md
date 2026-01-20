# HTTP Server in C

A lightweight HTTP server implemented in C as part of an advanced systems / programming course.

This project focuses on low-level networking concepts, including TCP socket programming, manual HTTP request parsing, and building server-side functionality without the use of external frameworks or libraries.

## Overview

The server listens on a TCP socket, accepts incoming client connections, parses basic HTTP requests, and returns valid HTTP responses. The goal of the project was to gain hands-on experience with systems-level programming and understand how higher-level web servers work under the hood.

This project emphasizes correctness, simplicity, and direct interaction with the operating system through standard C and POSIX APIs.

## Features

- TCP socket programming
- Accepts and handles client connections
- Parses basic HTTP GET requests
- Sends valid HTTP responses
- Minimal, dependency-free implementation
- Written entirely in standard C

## Project Structure

```
.
├── http-server.c   
├── Makefile   
└── README.md   
```
