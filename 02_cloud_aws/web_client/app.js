(() => {
  "use strict";

  const cfg = window.PSI3542_CONFIG || {};
  const el = (id) => document.getElementById(id);
  const apiInput = el("apiBaseUrl");
  const thingName = el("thingName");

  const savedApi = sessionStorage.getItem("psi3542.a11.apiBaseUrl") || "";
  const savedThing = sessionStorage.getItem("psi3542.a11.thingName") || "";

  apiInput.value = savedApi || cfg.apiBaseUrl || "";
  thingName.value = savedThing || cfg.defaultThingName || "";

  function currentApiBaseUrl() {
    const value = apiInput.value.trim().replace(/\/$/, "");
    if (!/^https:\/\/[A-Za-z0-9.-]+(?::\d+)?(?:\/[^\s]*)?$/.test(value)) {
      throw new Error("API Base URL inválida. Use uma URL HTTPS.");
    }
    sessionStorage.setItem("psi3542.a11.apiBaseUrl", value);
    return value;
  }

  function currentThingName() {
    const value = thingName.value.trim();
    if (!/^psi3542-[A-Za-z0-9_-]+$/.test(value)) {
      throw new Error("Thing Name inválido.");
    }
    sessionStorage.setItem("psi3542.a11.thingName", value);
    return value;
  }

  async function refreshTelemetry() {
    const status = el("status");
    try {
      const apiBaseUrl = currentApiBaseUrl();
      const name = currentThingName();
      status.textContent = "Consultando...";
      const response = await fetch(`${apiBaseUrl}/devices/${encodeURIComponent(name)}/telemetry/latest`);
      const body = await response.json();
      if (!response.ok) {
        throw new Error(body.error || `HTTP ${response.status}`);
      }
      el("temperature").textContent = `${body.temperature_c} °C`;
      el("humidity").textContent = `${body.humidity_pct} %`;
      el("seq").textContent = String(body.seq);
      el("ingestedAt").textContent = body.ingested_at || "—";
      const measurementValid = body.valid === true;
      el("measurementValidity").textContent = measurementValid
        ? "Válida"
        : "Inválida — não usar esta amostra como medição confiável";
      status.textContent = `Telemetria recebida para ${body.thingName}.`;
    } catch (err) {
      status.textContent = `Erro: ${err.message}`;
    }
  }

  async function sendLedCommand(value) {
    const status = el("commandStatus");
    try {
      const apiBaseUrl = currentApiBaseUrl();
      const name = currentThingName();
      status.textContent = "Enviando comando...";
      const response = await fetch(`${apiBaseUrl}/devices/${encodeURIComponent(name)}/command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ led: value })
      });
      const body = await response.json();
      if (!response.ok) {
        throw new Error(body.error || `HTTP ${response.status}`);
      }
      status.textContent = body.accepted
        ? `Comando ${value ? "LIGAR" : "DESLIGAR"} aceito pelo backend.`
        : "O backend não confirmou a aceitação do comando.";
    } catch (err) {
      status.textContent = `Erro: ${err.message}`;
    }
  }

  el("clearConfigBtn").addEventListener("click", () => {
    sessionStorage.removeItem("psi3542.a11.apiBaseUrl");
    sessionStorage.removeItem("psi3542.a11.thingName");
    apiInput.value = "";
    thingName.value = "";
    for (const id of ["temperature", "humidity", "seq", "ingestedAt", "measurementValidity"]) {
      el(id).textContent = "—";
    }
    el("status").textContent = "Configuração desta aba apagada.";
    el("commandStatus").textContent = "Nenhum comando enviado.";
  });
  el("refreshBtn").addEventListener("click", refreshTelemetry);
  el("ledOnBtn").addEventListener("click", () => sendLedCommand(true));
  el("ledOffBtn").addEventListener("click", () => sendLedCommand(false));
})();
