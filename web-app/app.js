"use strict";

/**
 * Look up a control in the tracker panel.
 * @param {string} id - Element ID in the page.
 * @returns {HTMLElement} The matching control.
 */
function byId(id) {
    return document.getElementById(id);
}

const SETTINGS = byId("settings");
const RESULT = byId("result");
const STATUS_LINE = byId("status");
let client = null;
let library = null;
let attempt = 0;

/**
 * Load the MQTT client on demand and share the pending download.
 * @returns {Promise<object>} The MQTT.js browser API.
 */
function loadMqtt() {
    if (!library) {
        library = new Promise((resolve, reject) => {
            const SCRIPT = document.createElement("script");
            SCRIPT.src =
                "https://cdn.jsdelivr.net/npm/mqtt@5.16.0/dist/mqtt.min.js";
            SCRIPT.onload = () => resolve(window.mqtt);
            SCRIPT.onerror = () => {
                SCRIPT.remove();
                library = null;
                reject(new Error(
                    "Could not load the MQTT client. " +
                    "Check your internet connection."
                ));
            };
            document.head.append(SCRIPT);
        });
    }
    return library;
}

/**
 * Close the current session and restore offline preview.
 * @param {string} message - Connection status to display.
 * @returns {void}
 */
function disconnect(message) {
    attempt++;
    const PREVIOUS_CLIENT = client;
    client = null;
    PREVIOUS_CLIENT?.end(true);
    SETTINGS.disabled = false;
    byId("toggle").textContent = "Connect";
    STATUS_LINE.textContent = message;
    RESULT.textContent =
        "Offline preview. Connect to HiveMQ to send commands.";
}

byId("connection").addEventListener("submit", async (event) => {
    event.preventDefault();
    if (SETTINGS.disabled) {
        disconnect("Disconnected");
        return;
    }

    const TOPIC = byId("topic");
    if (!TOPIC.value.trim() || /[+#\0]/.test(TOPIC.value)) {
        TOPIC.setCustomValidity(
            "Enter a command topic without MQTT wildcards."
        );
        TOPIC.reportValidity();
        return;
    }

    const CURRENT_ATTEMPT = ++attempt;
    SETTINGS.disabled = true;
    byId("toggle").textContent = "Cancel";
    STATUS_LINE.textContent = "Connecting…";
    try {
        const MQTT = await loadMqtt();
        if (CURRENT_ATTEMPT !== attempt) {
            return;
        }
        client = MQTT.connect(byId("broker").value, {
            username: byId("username").value || undefined,
            password: byId("password").value || undefined,
            reconnectPeriod: 0,
            connectTimeout: 10000,
        });
        client.on("connect", () => {
            if (CURRENT_ATTEMPT !== attempt) {
                return;
            }
            byId("toggle").textContent = "Disconnect";
            STATUS_LINE.textContent = "Connected to HiveMQ";
            RESULT.textContent = `Ready to publish to ${TOPIC.value.trim()}.`;
        });
        client.on("error", (error) => {
            if (CURRENT_ATTEMPT === attempt) {
                disconnect(`Connection error: ${error.message}`);
            }
        });
        client.on("close", () => {
            if (CURRENT_ATTEMPT === attempt) {
                disconnect(
                    "Connection closed. Check the broker URL and credentials."
                );
            }
        });
    } catch (error) {
        if (CURRENT_ATTEMPT === attempt) {
            disconnect(error.message);
        }
    }
});

byId("topic").addEventListener("input", (event) => {
    event.target.setCustomValidity("");
});

byId("command-buttons").addEventListener("click", (event) => {
    const COMMAND = event.target.dataset.command;
    if (!COMMAND) {
        return;
    }
    if (!client?.connected) {
        RESULT.textContent = `Offline preview · ${COMMAND}`;
        return;
    }
    const SENDER = client;
    const TOPIC = byId("topic").value.trim();
    RESULT.textContent = `Publishing ${COMMAND}…`;
    SENDER.publish(TOPIC, COMMAND, { qos: 1, retain: false }, (error) => {
        if (SENDER !== client) {
            return;
        }
        RESULT.textContent = error
            ? `Publish failed: ${error.message}`
            : `Broker acknowledged “${COMMAND}” on ${TOPIC}.`;
    });
});
