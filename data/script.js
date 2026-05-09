var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    initButtons();
}

function initWebSocket() {
    console.log('Intentando abrir conexión WebSocket...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // Usamos esta única función
}

function onOpen(event) {
    console.log('Conexión abierta');
    const status = document.getElementById('status');
    if(status){
        status.innerText = "Conectado en Tiempo Real";
        status.style.color = "#4caf50";
    }
}

function onClose(event) {
    console.log('Conexión cerrada');
    const status = document.getElementById('status');
    if(status){
        status.innerText = "Desconectado. Reconectando...";
        status.style.color = "#f44336";
    }
    setTimeout(initWebSocket, 2000); 
}

// === ESTA ES LA FUNCIÓN CRÍTICA ===
function onMessage(event) {
    let data = JSON.parse(event.data);
    console.log("Dato recibido:", data);

    // Coincide con doc["type"] = "sensor_update" del .cpp
    if (data.type === "sensor_update") {
        // IMPORTANTE: Verifica que en tu HTML el ID sea 'temp-value'
        const tempElement = document.getElementById('temp-value');
        if (tempElement) {
            tempElement.innerText = data.value.toFixed(1) + " °C";
            
            // Cambio de color dinámico
            if (data.value > 30) {
                tempElement.style.color = "red";
            } else {
                tempElement.style.color = "#2c3e50";
            }
        }
    }
}

function initButtons() {
    // 1. Botón del Extractor
    const btnExtractor = document.getElementById('btn-extractor');
    if (btnExtractor) {
        btnExtractor.addEventListener('click', () => {
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                websocket.send(JSON.stringify({action: "toggle_extractor"}));
            }
        });
    }

    // 2. Botón del LED (Ahora por WebSocket también)
    const btnLed = document.getElementById('btnLed');
    if (btnLed) {
        btnLed.addEventListener('click', () => {
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                // Coincide con el action == "toggle_led" que agregamos al .cpp
                websocket.send(JSON.stringify({action: "toggle_led"}));
            }
        });
    }
}