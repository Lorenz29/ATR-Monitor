let gateway = `ws://${window.location.hostname}/ws`;
let websocket;
let umbralAlertaActual = 1000; // Será sobreescrito en el primer mensaje de telemetría

window.addEventListener('load', initWebSocket);

function initWebSocket() {
    console.log('Iniciando conexión WebSocket...');
    websocket = new WebSocket(gateway);
    websocket.onmessage = onMessage;
    websocket.onclose = () => { setTimeout(initWebSocket, 2000); }; // Reconexión automática
}

function onMessage(event) {
    let data = JSON.parse(event.data);
    
    if (data.type === "system_update") {
        
        // 1. Actualizar Temperatura
        if(data.temp !== undefined) {
            document.getElementById("temp-val").innerText = data.temp.toFixed(1) + " °C";
        }
        
        // 2. Actualizar Select de Modo y Fan
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

        // 3. Sincronizar Estados de Botones
        if(data.relay1 !== undefined) updateButtonState("btn-relay1", data.relay1, "Fuente ATX: ON", "Fuente ATX: OFF");
        if(data.relay2 !== undefined) updateButtonState("btn-relay2", data.relay2, "Línea 12v Aux 1: ON", "Línea 12v Aux 1: OFF");
        if(data.relay3 !== undefined) updateButtonState("btn-relay3", data.relay3, "Línea 12v Aux 2: ON", "Línea 12v Aux 2: OFF");
        if(data.ledStatus !== undefined) updateButtonState("btn-led", data.ledStatus, "LED Placa: ON", "LED Placa: OFF");

        // 4. Datos Dinámicos de Calibración
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
                calStatus.style.color = "#f39c12"; // Naranja para advertencia
            }
        }

        // 5. Actualizar MQ-2 y Lógica de Alarma
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
                // Comparamos usando la variable dinámica que vino del backend
                if (data.mq2 > umbralAlertaActual) {
                    smokeStatus.innerText = "Estado: Humo detectado (Silenciado)";
                    smokeStatus.style.color = "#f39c12"; // Naranja
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

function toggleRelay1() { websocket.send(JSON.stringify({ "action": "toggleRelay1" })); }
function toggleRelay2() { websocket.send(JSON.stringify({ "action": "toggleRelay2" })); }
function toggleRelay3() { websocket.send(JSON.stringify({ "action": "toggleRelay3" })); }
function toggleLed()    { websocket.send(JSON.stringify({ "action": "toggleLed" })); }

function changeMode() {
    const modeVal = document.getElementById("mode-select").value;
    const isAuto = (modeVal === "auto");
    websocket.send(JSON.stringify({ "action": "setMode", "value": isAuto }));
}

function updateSliderTxt(val) {
    document.getElementById("fan-speed-txt").innerText = val;
}

function sendFanSpeed(val) {
    websocket.send(JSON.stringify({ "action": "setSpeed", "value": parseInt(val) }));
}

function stopAlarm() {
    websocket.send(JSON.stringify({ "action": "stopAlarm" }));
}

// Nueva función para enviar el umbral tipeado por el usuario
function sendUmbral() {
    const val = document.getElementById("umbral-input").value;
    if (val && val >= 0 && val <= 4095) {
        websocket.send(JSON.stringify({
            "action": "setUmbral",
            "value": parseInt(val)
        }));
        // Limpiamos el input visualmente
        document.getElementById("umbral-input").value = "";
    } else {
        alert("Por favor ingrese un valor de umbral válido (0 - 4095)");
    }
}