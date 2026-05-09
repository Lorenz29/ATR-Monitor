var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    initButtons();
}

// === WEBSOCKETS ===
function initWebSocket() {
    console.log('Intentando abrir conexión WebSocket...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Conexión abierta');
    document.getElementById('status').innerText = "Conectado en Tiempo Real";
    document.getElementById('status').style.color = "#4caf50";
}

function onClose(event) {
    console.log('Conexión cerrada');
    document.getElementById('status').innerText = "Desconectado. Reconectando...";
    document.getElementById('status').style.color = "#f44336";
    setTimeout(initWebSocket, 2000); 
}

function onMessage(event) {
    let data = JSON.parse(event.data);
    if(data.temp) {
        document.getElementById('temp-val').innerText = data.temp + " °C";
    }
}

// === LÓGICA DE BOTONES ===
function initButtons() {
    // 1. Botón del Extractor (Usa WebSockets)
    const btnExtractor = document.getElementById('btn-extractor');
    if (btnExtractor) {
        btnExtractor.addEventListener('click', () => {
            // Mandar orden a la placa
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                websocket.send(JSON.stringify({action: "toggle_extractor"}));
            }
            
            // Cambiar aspecto visual
            let isOn = btnExtractor.getAttribute('data-state') === 'on';
            if (!isOn) {
                btnExtractor.classList.add("on");
                btnExtractor.innerText = "Encendido";
                btnExtractor.setAttribute('data-state', 'on');
            } else {
                btnExtractor.classList.remove("on");
                btnExtractor.innerText = "Apagado";
                btnExtractor.setAttribute('data-state', 'off');
            }
        });
    }

    // 2. Botón del LED (Usa Petición HTTP Fetch)
    const btnLed = document.getElementById('btnLed');
    if (btnLed) {
        btnLed.addEventListener('click', () => {
            fetch('/toggle')
                .then(response => {
                    if (!response.ok) throw new Error("Error en el servidor");
                    return response.text();
                })
                .then(estado => console.log("ESP32 dice: LED " + estado))
                .catch(error => console.error("Error al comunicar con la placa:", error));
        });
    }
}