#pragma once
#include <Arduino.h>
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>

// =============================================================================
// WebView: tiny dependency-free HTTP + Server-Sent-Events server.
//
// Serves the phone radar page and streams live frames to it, so a phone on the
// same WiFi can stand in for the external TFT. Built straight on lwIP sockets:
// every recv/send is MSG_DONTWAIT, so poll() never blocks loop(), even when
// a phone goes to sleep with the stream open (its socket is dropped after
// kStallMs of no progress).
//
//   GET /        -> the embedded page
//   GET /ev      -> text/event-stream (hello payload, then broadcast() events)
//   GET /cmd?... -> onCmd(query), answers 204
//
// No String / no heap churn: fixed per-connection buffers (no PSRAM).
// =============================================================================
class WebView {
public:
  static constexpr int      kMaxConns = 5;
  static constexpr int      kMaxSse   = 3;
  static constexpr int      kReqMax   = 512;
  static constexpr int      kOutMax   = 2048;
  static constexpr uint32_t kReadMs   = 4000;   // idle/preconnect sockets
  static constexpr uint32_t kStallMs  = 6000;   // send made no progress

  using CmdFn   = void   (*)(const char* query);
  using HelloFn = size_t (*)(char* buf, size_t cap);   // raw SSE text for a new client

  bool begin(uint16_t port, const uint8_t* page, size_t pageLen, CmdFn onCmd, HelloFn onHello) {
    _page = page; _pageLen = pageLen; _onCmd = onCmd; _onHello = onHello;
    _lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (_lfd < 0) return false;
    int one = 1;
    setsockopt(_lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(_lfd, (sockaddr*)&a, sizeof(a)) < 0 || listen(_lfd, 4) < 0) {
      close(_lfd); _lfd = -1; return false;
    }
    fcntl(_lfd, F_SETFL, O_NONBLOCK);
    return true;
  }

  // Accept, read requests, flush pending output. Call every loop().
  void poll() {
    if (_lfd < 0) return;
    const uint32_t now = millis();

    for (;;) {
      int fd = accept(_lfd, nullptr, nullptr);
      if (fd < 0) break;
      Conn* c = freeSlot();
      if (!c) { close(fd); continue; }
      fcntl(fd, F_SETFL, O_NONBLOCK);
      int one = 1;
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
      c->fd = fd; c->st = READ; c->t = now; c->rlen = 0;
      c->olen = c->ooff = 0; c->body = nullptr; c->blen = c->boff = 0;
    }

    for (auto& c : _c) {
      if (c.st == FREE) continue;
      if (c.st == READ) {
        int n = recv(c.fd, c.req + c.rlen, kReqMax - 1 - c.rlen, MSG_DONTWAIT);
        if (n > 0) {
          c.rlen += n; c.req[c.rlen] = '\0';
          if (strstr(c.req, "\r\n\r\n") || c.rlen >= kReqMax - 1) route(c, now);
        } else if (n == 0 || !wouldBlock()) {
          drop(c);
        } else if (now - c.t > kReadMs) {
          drop(c);
        }
        continue;
      }
      // SEND / SSE: discard anything the client sends; notice when it hangs up.
      char junk[64];
      int n = recv(c.fd, junk, sizeof(junk), MSG_DONTWAIT);
      if (n == 0 || (n < 0 && !wouldBlock())) { drop(c); continue; }
      flush(c, now);
      if (c.st == FREE) continue;
      if (pending(c) && now - c.t > kStallMs) { drop(c); continue; }
      if (c.st == SEND && !pending(c)) drop(c);
    }
  }

  // Send one SSE event to every stream. A client still busy with an earlier
  // event skips this one (frames are cheap to lose, lag is not).
  void broadcast(const char* event, const char* data) {
    char head[24];
    int  hl = event ? snprintf(head, sizeof(head), "event: %s\n", event) : 0;
    const int dl = (int)strlen(data);
    const int total = hl + 6 + dl + 2;              // "data: " ... "\n\n"
    const uint32_t now = millis();
    for (auto& c : _c) {
      if (c.st != SSE) continue;
      if (c.ooff > 0 && c.ooff == c.olen) c.olen = c.ooff = 0;
      if (c.olen - c.ooff > 0 && c.ooff > 0) {       // compact the remainder
        memmove(c.out, c.out + c.ooff, c.olen - c.ooff);
        c.olen -= c.ooff; c.ooff = 0;
      }
      if (c.olen + total > kOutMax) continue;
      if (c.olen > 0 && !event) continue;           // drop stale frames, keep status
      if (!pending(c)) c.t = now;                   // stall timer counts from first unsent byte
      memcpy(c.out + c.olen, head, hl);           c.olen += hl;
      memcpy(c.out + c.olen, "data: ", 6);        c.olen += 6;
      memcpy(c.out + c.olen, data, dl);           c.olen += dl;
      memcpy(c.out + c.olen, "\n\n", 2);          c.olen += 2;
      flush(c, now);
    }
  }

  int sseClients() const {
    int n = 0;
    for (auto& c : _c) if (c.st == SSE) n++;
    return n;
  }
  uint32_t sseConnects() const { return _sseConnects; }

private:
  enum : uint8_t { FREE, READ, SEND, SSE };
  struct Conn {
    int            fd   = -1;
    uint8_t        st   = FREE;
    uint32_t       t    = 0;       // last progress (ms)
    uint32_t       born = 0;       // when the stream started (eviction order)
    char           req[kReqMax];
    uint16_t       rlen = 0;
    char           out[kOutMax];   // headers / events not yet sent
    uint16_t       olen = 0, ooff = 0;
    const uint8_t* body = nullptr; // page bytes in flash
    size_t         blen = 0, boff = 0;
  };

  static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
  static bool pending(const Conn& c) { return c.ooff < c.olen || c.boff < c.blen; }

  Conn* freeSlot() {
    for (auto& c : _c) if (c.st == FREE) return &c;
    // Full: recycle a socket that never sent a request (browser preconnect).
    Conn* idle = nullptr;
    for (auto& c : _c) if (c.st == READ && c.rlen == 0 && (!idle || c.t < idle->t)) idle = &c;
    if (idle) { drop(*idle); return idle; }
    return nullptr;
  }

  void drop(Conn& c) {
    if (c.fd >= 0) close(c.fd);
    c.fd = -1; c.st = FREE; c.rlen = 0; c.olen = c.ooff = 0;
    c.body = nullptr; c.blen = c.boff = 0;
  }

  void flush(Conn& c, uint32_t now) {
    while (c.ooff < c.olen) {
      int n = send(c.fd, c.out + c.ooff, c.olen - c.ooff, MSG_DONTWAIT);
      if (n > 0) { c.ooff += n; c.t = now; continue; }
      if (n < 0 && wouldBlock()) return;
      drop(c); return;
    }
    c.olen = c.ooff = 0;
    while (c.boff < c.blen) {
      size_t chunk = c.blen - c.boff;
      if (chunk > 2920) chunk = 2920;
      int n = send(c.fd, c.body + c.boff, chunk, MSG_DONTWAIT);
      if (n > 0) { c.boff += n; c.t = now; continue; }
      if (n < 0 && wouldBlock()) return;
      drop(c); return;
    }
  }

  void respond(Conn& c, const char* status, const char* type, size_t len) {
    c.olen = snprintf(c.out, kOutMax,
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, type, (unsigned)len);
    c.ooff = 0;
    c.st   = SEND;
  }

  void route(Conn& c, uint32_t now) {
    c.t = now;
    // Request line: "GET /path?query HTTP/1.1"
    char* sp = strchr(c.req, ' ');
    if (!sp || strncmp(c.req, "GET ", 4) != 0) {
      respond(c, "405 Method Not Allowed", "text/plain", 0); return;
    }
    char* path = sp + 1;
    char* end  = strchr(path, ' ');
    if (end) *end = '\0';
    char* query = strchr(path, '?');
    if (query) *query++ = '\0';

    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
      respond(c, "200 OK", "text/html; charset=utf-8", _pageLen);
      c.body = _page; c.blen = _pageLen; c.boff = 0;
    } else if (!strcmp(path, "/ev")) {
      startStream(c, now);
    } else if (!strcmp(path, "/cmd")) {
      if (_onCmd) _onCmd(query ? query : "");
      c.olen = snprintf(c.out, kOutMax,
          "HTTP/1.1 204 No Content\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
      c.ooff = 0; c.st = SEND;
    } else {
      respond(c, "404 Not Found", "text/plain", 0);
    }
    flush(c, now);
  }

  void startStream(Conn& c, uint32_t now) {
    // A reloaded page leaves its old stream half-open for a while; evict the
    // oldest stream instead of refusing the new one.
    if (sseClients() >= kMaxSse) {
      Conn* old = nullptr;
      for (auto& o : _c) if (o.st == SSE && (!old || o.born < old->born)) old = &o;
      if (old) drop(*old);
    }
    int n = snprintf(c.out, kOutMax,
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
        "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\nretry: 1500\n\n");
    if (_onHello && n < kOutMax) n += (int)_onHello(c.out + n, kOutMax - n);
    c.olen = (uint16_t)n; c.ooff = 0;
    c.st = SSE; c.born = now;
    _sseConnects++;
  }

  Conn           _c[kMaxConns];
  int            _lfd       = -1;
  const uint8_t* _page      = nullptr;
  size_t         _pageLen   = 0;
  CmdFn          _onCmd     = nullptr;
  HelloFn        _onHello   = nullptr;
  uint32_t       _sseConnects = 0;
};
