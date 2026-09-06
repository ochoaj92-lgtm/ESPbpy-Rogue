"use strict";

const canvas = document.getElementById("canvas");
const statusLine = document.getElementById("status");
const gameButtons = [...document.querySelectorAll("button[data-key]")];
const heldPointers = new Map();
let ready = false;

function setStatus(message, error = false) {
  statusLine.textContent = message;
  statusLine.classList.toggle("error", error);
}

function sendButton(button, down) {
  if (ready) window.Module._pocket_key(Number(button.dataset.key), down ? 1 : 0);
  button.classList.toggle("active", down);
  button.setAttribute("aria-pressed", String(down));
}

function releaseAll() {
  for (const button of gameButtons) sendButton(button, false);
  heldPointers.clear();
}

window.Module = {
  canvas,
  print: (...values) => console.log(...values),
  printErr: (...values) => console.error(...values),
  onRuntimeInitialized() {
    ready = true;
    gameButtons.forEach(button => { button.disabled = false; });
    setStatus("Ready · press Enter twice to deal in");
  },
  onAbort(reason) {
    releaseAll();
    ready = false;
    gameButtons.forEach(button => { button.disabled = true; });
    setStatus("The game could not start. Refresh to retry.", true);
    console.error("Game startup failed:", reason);
  }
};

for (const button of gameButtons) {
  button.setAttribute("aria-pressed", "false");
  button.addEventListener("pointerdown", event => {
    if (!ready) return;
    event.preventDefault();
    canvas.focus({ preventScroll: true });
    button.setPointerCapture(event.pointerId);
    heldPointers.set(event.pointerId, button);
    sendButton(button, true);
  });
  function releasePointer(event) {
    const pressed = heldPointers.get(event.pointerId);
    if (!pressed) return;
    heldPointers.delete(event.pointerId);
    if (![...heldPointers.values()].includes(pressed)) sendButton(pressed, false);
  }
  button.addEventListener("pointerup", releasePointer);
  button.addEventListener("pointercancel", releasePointer);
  button.addEventListener("lostpointercapture", releasePointer);
  // Keyboard/assistive activation of a focused page button behaves like a tap.
  button.addEventListener("click", event => {
    if (event.detail !== 0 || !ready) return;
    sendButton(button, true);
    setTimeout(() => sendButton(button, false), 60);
  });
}

canvas.addEventListener("pointerdown", () => canvas.focus({ preventScroll: true }));
canvas.addEventListener("blur", () => {
  releaseAll();
  if (ready) window.Module._pocket_release();
});
canvas.addEventListener("contextmenu", event => event.preventDefault());
const playKeys = new Set(["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", " ", "Enter", "Escape", "z", "Z", "x", "X", "q", "Q", "e", "E"]);
for (const name of ["keydown", "keyup"]) {
  window.addEventListener(name, event => {
    if (!playKeys.has(event.key)) return;
    if (document.activeElement === canvas) event.preventDefault();
    // SDL listens on the document. Let focused HTML buttons keep their native
    // Enter/Space activation, without also sending A to the game.
    else event.stopPropagation();
  }, true);
}
window.addEventListener("blur", releaseAll);
document.addEventListener("visibilitychange", () => { if (document.hidden) releaseAll(); });
canvas.addEventListener("webglcontextlost", event => {
  event.preventDefault();
  releaseAll();
  ready = false;
  gameButtons.forEach(button => { button.disabled = true; });
  setStatus("The display was interrupted. Refresh to restart the demo.", true);
});

const gameScript = document.createElement("script");
gameScript.src = "./game.js";
gameScript.onerror = () => setStatus("The game download failed. Refresh to retry.", true);
document.body.appendChild(gameScript);
