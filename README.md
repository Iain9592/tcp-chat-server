# TCP Chat Server (C)

A concurrent multi-client TCP chat server implemented in C using socket programming and `select()`-based I/O multiplexing. The server supports multiple connected clients, custom messaging commands, and channel-based communication without using threads.

---

## 🚀 Features

* Multi-client support using `select()` (no threads or fork)
* Event-driven architecture for efficient I/O handling
* Custom text-based protocol
* Channel-based messaging system
* Private messaging between users
* Robust handling of partial and multiple messages per `recv()`
* Graceful client connection and disconnection handling

---

## 🧠 Supported Commands

* `LOGIN <username>` – authenticate with a unique username
* `JOIN <channel>` – join or create a channel
* `MSG <message>` – send message to current channel
* `PM <username> <message>` – send private message
* `WHO` – list users in current channel
* `QUIT` – disconnect from server

---

## ⚙️ How It Works

* Uses TCP sockets (`socket`, `bind`, `listen`, `accept`) for networking
* Uses `select()` to monitor multiple client sockets concurrently
* Maintains per-client buffers to handle:

  * partial messages
  * multiple commands in a single `recv()`
* Implements a newline-delimited protocol for command parsing
* Tracks client state (login status, channel membership)

---

## 🛠️ Build & Run

### Compile

```bash
make
```

### Run server

```bash
./server <port>
```

Example:

```bash
./server 5555
```

### Run client (in another terminal)

```bash
./client 127.0.0.1 <port>
```

---

## 📌 Example Usage

```
Username: Alice
/join General
Hello everyone!
```

```
Username: Bob
/join General
Hi Alice!
```

---

## 🧪 Tech Stack

* C (C11)
* POSIX sockets API
* `select()` for I/O multiplexing
* Linux environment

---

## 📈 Key Concepts Demonstrated

* Network programming (TCP/IP)
* Concurrent client handling without threads
* Event-driven system design
* Protocol design and parsing
* Buffer management and edge-case handling

---

## 📄 Notes

This project focuses on low-level networking and concurrent system design in C.

---

## 🔗 Authors

Iain Campbell & Soumil Nag
