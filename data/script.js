var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
let audioCtx, oscillator; // Variables para el sonido

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    initButtons();
}

function initWebSocket() {
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    document.getElementById('status').innerText = "Conectado";
    document.getElementById('status').style.color = "#4caf50";
}

function onClose(event) {
    document.getElementById('status').innerText = "Desconectado";
    setTimeout(initWebSocket, 2000);
}

// En tu script.js, actualizá el umbral
const UMBRAL_ALERTA = 550; // Debe ser igual al que definiste en el main.cpp

function onMessage(event) {
    let data = JSON.parse(event.data);
    
    if (data.type === "sensor_update") {
        // ... (resto del código de temperatura)

        // Lógica actualizada de alarma
        if (data.smoke !== undefined) {
            // Ahora comparamos contra la misma constante
            if (data.smoke > UMBRAL_ALERTA) { 
                activarAlarma();
            } else {
                desactivarAlarma();
            }
        }
    }
}

// --- Funciones de Alarma ---
function activarAlarma() {
    document.getElementById('alarm-overlay').className = 'alarm-overlay-active';
    
    // Iniciar sonido solo si no está iniciado
    if (!audioCtx) {
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        oscillator = audioCtx.createOscillator();
        oscillator.type = 'sawtooth';
        oscillator.frequency.setValueAtTime(800, audioCtx.currentTime);
        oscillator.connect(audioCtx.destination);
        oscillator.start();
    }
}

function desactivarAlarma() {
    document.getElementById('alarm-overlay').className = 'alarm-overlay-hidden';
    
    // Detener sonido
    if (audioCtx) {
        oscillator.stop();
        oscillator.disconnect();
        audioCtx = null;
        oscillator = null;
    }
}

function initButtons() {
    document.getElementById('btn-extractor').addEventListener('click', () => {
        websocket.send(JSON.stringify({action: "toggle_extractor"}));
    });
    document.getElementById('btnLed').addEventListener('click', () => {
        websocket.send(JSON.stringify({action: "toggle_led"}));
    });
}