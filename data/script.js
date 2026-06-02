// ¡IMPORTANTE! Debe coincidir con el umbral seteado en tu archivo main.cpp
const UMBRAL_ALERTA = 1000; 

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
    
    if (data.type === "status_update" || data.type === "sensor_update") {
        
        // 1. Actualizar Temperatura (si viene en el JSON)
        if(data.temperature !== undefined) {
            document.getElementById("temp-val").innerText = data.temperature.toFixed(1) + " °C";
        }
        
        // 2. Actualizar Select de Modo y Fan
        if(data.autoMode !== undefined) {
            const modeSelect = document.getElementById("mode-select");
            const fanSlider = document.getElementById("fan-slider");
            
            if (data.autoMode) {
                modeSelect.value = "auto";
                fanSlider.disabled = true; // Bloquea el slider en modo automático
            } else {
                modeSelect.value = "manual";
                fanSlider.disabled = false; // Desbloquea en modo manual
            }
        }
        
        if(data.fanSpeed !== undefined) {
            document.getElementById("fan-slider").value = data.fanSpeed;
            document.getElementById("fan-speed-txt").innerText = data.fanSpeed;
        }

        // 3. Actualizar MQ-2 y Lógica de Alarma
        if (data.smoke !== undefined) {
            document.getElementById("smoke-val").innerText = data.smoke;
            
            const smokeStatus = document.getElementById("smoke-status");
            const alarmOverlay = document.getElementById("alarm-overlay");

            // Disparamos la alarma si el humo supera el umbral o si el ESP32 manda la flag isAlarm = true
            if (data.smoke > UMBRAL_ALERTA || data.isAlarm === true) {
                smokeStatus.innerText = "Estado: ¡PELIGRO!";
                smokeStatus.style.color = "red";
                smokeStatus.style.fontWeight = "bold";
                
                // Activar pantalla roja parpadeante
                alarmOverlay.classList.remove("alarm-overlay-hidden");
                alarmOverlay.classList.add("alarm-overlay-active");
            } else {
                smokeStatus.innerText = "Estado: Normal";
                smokeStatus.style.color = "var(--status-ok)";
                smokeStatus.style.fontWeight = "normal";
                
                // Ocultar pantalla roja
                alarmOverlay.classList.add("alarm-overlay-hidden");
                alarmOverlay.classList.remove("alarm-overlay-active");
            }
        }
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

// Permite silenciar la alarma o reiniciar el estado de alerta manualmente
function stopAlarm() {
    websocket.send(JSON.stringify({ "action": "stopAlarm" }));
    // Escondemos momentáneamente la alerta, aunque si el ESP32 sigue censando humo, volverá a activarla
    document.getElementById("alarm-overlay").classList.add("alarm-overlay-hidden");
    document.getElementById("alarm-overlay").classList.remove("alarm-overlay-active");
}