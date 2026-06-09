let gateway = `ws://${window.location.hostname}/ws`;
let websocket;
let umbralAlertaActual = 1000; 

// --- Estado Global del Sistema UI ---
// Corrección N° 3: Siempre arranca apagado al refrescar el explorador.
let systemActive = false;

window.addEventListener('load', () => {
    initWebSocket();
    applySystemState(); // Ejecuta el estado visual inicial (Off)
});

// --- Control del Botón Principal ---
function toggleSystem() {
    websocket.send(JSON.stringify({ "action": "toggleRelay1" }));
    systemActive = !systemActive;
    applySystemState();
}

function applySystemState() {
    const btn = document.getElementById('power-btn');
    const subtitle = document.getElementById('subtitle');
    const dashboard = document.getElementById('dashboard');

    if (systemActive) {
        // Estado ON
        btn.className = 'power-on-mini';
        subtitle.style.display = 'none'; // El subtítulo desaparece
        dashboard.classList.remove('hidden'); // Aparece la ventana
    } else {
        // Estado OFF
        btn.className = 'power-off';
        subtitle.style.display = 'block'; // Vuelve la bajada de línea
        dashboard.classList.add('hidden'); // Se oculta la ventana
    }
}

// --- Navegación SPA de la Ventana ---
function showTab(tabId) {
    // 1. Ocultar todas las tarjetas (derecha)
    const tabs = document.querySelectorAll('.tab-content');
    tabs.forEach(tab => tab.classList.remove('active'));

    // 2. Despintar todos los botones (izquierda)
    const navBtns = document.querySelectorAll('.nav-btn');
    navBtns.forEach(btn => btn.classList.remove('active'));

    // 3. Mostrar la tarjeta clickeada
    document.getElementById(tabId).classList.add('active');

    // 4. Pintar el botón que hizo la llamada
    const activeBtn = Array.from(navBtns).find(btn => btn.getAttribute('onclick').includes(tabId));
    if (activeBtn) activeBtn.classList.add('active');
}

// --- Lógica de WebSockets (Comunicación con ESP32) ---
function initWebSocket() {
    console.log('Iniciando conexión WebSocket...');
    websocket = new WebSocket(gateway);
    websocket.onmessage = onMessage;
    websocket.onclose = () => { setTimeout(initWebSocket, 2000); }; 
}

function onMessage(event) {
    let data = JSON.parse(event.data);
    
    if (data.type === "system_update") {
        
        if(data.temp !== undefined) {
            document.getElementById("temp-val").innerText = data.temp.toFixed(1) + " °C";
        }
        
        if(data.autoMode !== undefined) {
            const modeSelect = document.getElementById("mode-select");
            const fanSlider = document.getElementById("fan-slider");
            if (data.autoMode) {
                modeSelect.value = "auto";
                fanSlider.disabled = true;
            } else {
                modeSelect.value = "manual";
                fanSlider.disabled = false;
            }
        }
        if(data.fanSpeed !== undefined) {
            document.getElementById("fan-slider").value = data.fanSpeed;
            document.getElementById("fan-speed-txt").innerText = data.fanSpeed;
        }
        
        if(data.relay1 !== undefined) {
            applySystemState(data.relay1);
        }
        // if(data.relay1 !== undefined) updateButtonState("btn-relay1", data.relay1, "Fuente ATX: ON", "Fuente ATX: OFF");
        if(data.relay2 !== undefined) updateButtonState("btn-relay2", data.relay2, "Línea 12v Aux 1: ON", "Línea 12v Aux 1: OFF");
        if(data.relay3 !== undefined) updateButtonState("btn-relay3", data.relay3, "Línea 12v Aux 2: ON", "Línea 12v Aux 2: OFF");
        if(data.ledStatus !== undefined) updateButtonState("btn-led", data.ledStatus, "LED Placa: ON", "LED Placa: OFF");

        if(data.umbral !== undefined) {
            umbralAlertaActual = data.umbral;
            document.getElementById("umbral-val").innerText = data.umbral;
        }
        if(data.calibrado !== undefined) {
            const calStatus = document.getElementById("cal-status");
            if (data.calibrado) {
                calStatus.innerText = "✓ Sensor Calibrado y Estable";
                calStatus.style.color = "var(--status-ok)";
            } else {
                calStatus.innerText = `⏳ Precalentando Filamento... (${data.tiempoCal}s)`;
                calStatus.style.color = "#f39c12"; 
            }
        }

        if (data.mq2 !== undefined) {
            document.getElementById("smoke-val").innerText = data.mq2;
            const smokeStatus = document.getElementById("smoke-status");
            const alarmOverlay = document.getElementById("alarm-overlay");

            if (data.alarm === true) {
                smokeStatus.innerText = "Estado: ¡PELIGRO!";
                smokeStatus.style.color = "red";
                smokeStatus.style.fontWeight = "bold";
                alarmOverlay.classList.remove("alarm-overlay-hidden");
                alarmOverlay.classList.add("alarm-overlay-active");
            } else {
                if (data.mq2 > umbralAlertaActual) {
                    smokeStatus.innerText = "Estado: Humo detectado (Silenciado)";
                    smokeStatus.style.color = "#f39c12"; 
                    smokeStatus.style.fontWeight = "bold";
                } else {
                    smokeStatus.innerText = "Estado: Normal";
                    smokeStatus.style.color = "var(--status-ok)";
                    smokeStatus.style.fontWeight = "normal";
                }
                alarmOverlay.classList.add("alarm-overlay-hidden");
                alarmOverlay.classList.remove("alarm-overlay-active");
            }
        }
    }
}

function updateButtonState(btnId, isOn, textOn, textOff) {
    const btn = document.getElementById(btnId);
    if (isOn) {
        btn.innerText = textOn;
        btn.classList.remove("btn-off");
        btn.classList.add("btn-on");
    } else {
        btn.innerText = textOff;
        btn.classList.remove("btn-on");
        btn.classList.add("btn-off");
    }
}

// Emisores WebSocket hacia el ESP32
function toggleRelay1() { websocket.send(JSON.stringify({ "action": "toggleRelay1" })); }
function toggleRelay2() { websocket.send(JSON.stringify({ "action": "toggleRelay2" })); }
function toggleRelay3() { websocket.send(JSON.stringify({ "action": "toggleRelay3" })); }
function toggleLed()    { websocket.send(JSON.stringify({ "action": "toggleLed" })); }

function changeMode() {
    const isAuto = (document.getElementById("mode-select").value === "auto");
    websocket.send(JSON.stringify({ "action": "setMode", "value": isAuto }));
}

function updateSliderTxt(val) { document.getElementById("fan-speed-txt").innerText = val; }

function sendFanSpeed(val) {
    websocket.send(JSON.stringify({ "action": "setSpeed", "value": parseInt(val) }));
}

function stopAlarm() { websocket.send(JSON.stringify({ "action": "stopAlarm" })); }

function sendUmbral() {
    const val = document.getElementById("umbral-input").value;
    if (val && val >= 0 && val <= 4095) {
        websocket.send(JSON.stringify({ "action": "setUmbral", "value": parseInt(val) }));
        document.getElementById("umbral-input").value = "";
    } else {
        alert("Por favor ingrese un valor válido (0 - 4095)");
    }
}