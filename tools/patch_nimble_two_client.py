#!/usr/bin/env python3
"""Give NimBLEServer one client per connection (MC-R39, MC-Q15).

NimBLE-Arduino keeps a single client on the server and routes notifications to it, so a second
bonded host can't run Apple Media Service alongside the first. PlatformIO runs this before each
build (extra_scripts in platformio.ini) against the pinned 2.5.2 in .pio/libdeps. It does nothing
on a copy that's already patched.
"""
import pathlib

try:
    Import("env")  # noqa: F821 - provided by PlatformIO
    ROOT = pathlib.Path(env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV/NimBLE-Arduino/src"))  # noqa: F821
except NameError:
    ROOT = pathlib.Path(".pio/libdeps/cyd2usb/NimBLE-Arduino/src")
FIND = """    for (NimBLEClient* c : {CLIENTS}) {
        if (c && c->m_connHandle == {HANDLE}) {
            {BODY}
        }
    }"""


def each(clients, handle, body):
    return FIND.replace("{CLIENTS}", clients).replace("{HANDLE}", handle).replace("{BODY}", body)


def swap(text, old, new):
    if old not in text:
        raise SystemExit(f"not found: {old[:60]}")
    return text.replace(old, new)


def patch():
    h = ROOT / "NimBLEServer.h"
    t = h.read_text()
    t = swap(t, "NimBLEClient* m_pClient{nullptr};", "NimBLEClient* m_pClients[3]{nullptr, nullptr, nullptr};")
    h.write_text(t)

    c = ROOT / "NimBLEServer.cpp"
    t = c.read_text()
    t = swap(t, """    if (m_pClient != nullptr) {
        delete m_pClient;
    }
# endif
}
""", """    for (NimBLEClient* c : m_pClients) {
        delete c;
    }
# endif
}
""")
    t = swap(t, """            if (pServer->m_pClient && pServer->m_pClient->m_connHandle == event->disconnect.conn.conn_handle) {
                // If this was also the client make sure it's flagged as disconnected.
                pServer->m_pClient->m_connHandle = BLE_HS_CONN_HANDLE_NONE;
            }
""", each("pServer->m_pClients", "event->disconnect.conn.conn_handle", "c->m_connHandle = BLE_HS_CONN_HANDLE_NONE;") + "\n")
    t = swap(t, """            if (pServer->m_pClient && pServer->m_pClient->m_connHandle == event->notify_rx.conn_handle) {
                NimBLEClient::handleGapEvent(event, pServer->m_pClient);
            }
""", each("pServer->m_pClients", "event->notify_rx.conn_handle", "NimBLEClient::handleGapEvent(event, c);") + "\n")
    t = swap(t, """            if (pServer->m_pClient && pServer->m_pClient->m_connHandle == event->enc_change.conn_handle) {
                NimBLEClient::handleGapEvent(event, pServer->m_pClient);
            }
""", each("pServer->m_pClients", "event->enc_change.conn_handle", "NimBLEClient::handleGapEvent(event, c);") + "\n")
    t = swap(t, """    if (m_pClient == nullptr) {
        m_pClient = new NimBLEClient(connInfo.getAddress());
    }

    m_pClient->deleteServices(); // Changed peer connection delete the database.
    m_pClient->m_peerAddress = connInfo.getAddress();
    m_pClient->m_connHandle  = connInfo.getConnHandle();
    m_pClient->m_connStatus  = NimBLEClient::CONNECTED;
    return m_pClient;
""", """    for (NimBLEClient* c : m_pClients) {
        if (c && c->m_connHandle == connInfo.getConnHandle()) {
            return c;
        }
    }

    for (NimBLEClient*& c : m_pClients) {
        if (c == nullptr) {
            c = new NimBLEClient(connInfo.getAddress());
        } else if (c->m_connHandle != BLE_HS_CONN_HANDLE_NONE) {
            continue;
        }

        c->deleteServices(); // Changed peer connection delete the database.
        c->m_peerAddress = connInfo.getAddress();
        c->m_connHandle  = connInfo.getConnHandle();
        c->m_connStatus  = NimBLEClient::CONNECTED;
        return c;
    }

    return nullptr;
""")
    t = swap(t, """    if (m_pClient != nullptr) {
        delete m_pClient;
        m_pClient = nullptr;
    }
} // deleteClient
""", """    for (NimBLEClient*& c : m_pClients) {
        delete c;
        c = nullptr;
    }
} // deleteClient
""")
    c.write_text(t)
    print("NimBLE patched: one client per connection")


if "m_pClients" not in (ROOT / "NimBLEServer.h").read_text():
    patch()
