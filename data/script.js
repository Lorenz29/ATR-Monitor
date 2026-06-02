let gateway = `ws://${window.location.hostname}/ws`;
let websocket;

window.addEventListener('load', initWebSocket);

function initWebSocket() {
    console.log('Iniciando conexión WebSocket...');
    websocket = new WebSocket(gateway);
    websocket.onmessage = onMessage;
    websocket.onclose = () => { setTimeout(initWebSocket, 2000); }; // Reconexión automática
}

function onMessage(event) {
    let data = JSON.parse(event.data);
    
    if (data.type === "status_update") {
        // 1. Actualizar Temperatura
        document.getElementById("temp-val").innerText = data.temperature.toFixed(1) + " °C";
        
        // 2. Actualizar Select de Modo
        const modeSelect = document.getElementById("mode-select");
        const fanSlider = document.getElementById("fan-slider");
        
        if (data.autoMode) {
            modeSelect.value = "auto";
            fanSlider.disabled = true; // Bloquea el slider en modo automático
        } else {
            modeSelect.value = "manual";
            fanSlider.disabled = false; // Desbloquea en modo manual
        }
        
        // 3. Actualizar el Slider de velocidad y su texto indicador
        fanSlider.value = data.fanSpeed;
        document.getElementById("fan-speed-txt").innerText = data.fanSpeed;
    }
}

// Cambia de Modo (Filtra la acción al ESP32)
function changeMode() {
    const modeVal = document.getElementById("mode-select").value;
    const isAuto = (modeVal === "auto");
    
    websocket.send(JSON.stringify({
        "action": "setMode",
        "value": isAuto
    }));
}

// Actualiza el texto en tiempo real mientras arrastras el slider
function updateSliderTxt(val) {
    document.getElementById("fan-speed-txt").innerText = val;
}

// Envía el valor definitivo del PWM al soltar el slider
function sendFanSpeed(val) {
    websocket.send(JSON.stringify({
        "action": "setSpeed",
        "value": parseInt(val)
    }));
}