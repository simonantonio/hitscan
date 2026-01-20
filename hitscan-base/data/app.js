// Race Timer Web Interface
class RaceTimer {
  constructor() {
    this.raceActive = false;
    this.startTime = 0;
    this.timerInterval = null;
    this.updateInterval = null;
    this.currentMode = "race";
    this.racers = [];

    this.initElements();
    this.attachEventListeners();
    this.loadRacers();
    this.loadMode();
    this.startPolling();
  }

  initElements() {
    this.startBtn = document.getElementById("startBtn");
    this.stopBtn = document.getElementById("stopBtn");
    this.resetBtn = document.getElementById("resetBtn");
    this.modeRaceBtn = document.getElementById("modeRace");
    this.modeLapBtn = document.getElementById("modeLap");
    this.editRacersBtn = document.getElementById("editRacersBtn");
    this.statusEl = document.getElementById("status");
    this.timerEl = document.getElementById("raceTimer");
    this.resultsEl = document.getElementById("results");
    this.lastUpdateEl = document.getElementById("lastUpdate");
    this.connectionEl = document.getElementById("connection");
  }

  attachEventListeners() {
    this.startBtn.addEventListener("click", () => this.startRace());
    this.stopBtn.addEventListener("click", () => this.stopRace());
    this.resetBtn.addEventListener("click", () => this.resetRace());
    this.modeRaceBtn.addEventListener("click", () => this.setMode("race"));
    this.modeLapBtn.addEventListener("click", () => this.setMode("lap"));
    this.editRacersBtn.addEventListener("click", () => this.editRacers());
  }

  async loadRacers() {
    try {
      const response = await fetch("/racers");
      this.racers = await response.json();
      this.updateRacerNames();
    } catch (error) {
      console.error("Failed to load racers:", error);
    }
  }

  async loadMode() {
    try {
      const response = await fetch("/mode");
      this.currentMode = await response.text();
      this.updateModeUI();
    } catch (error) {
      console.error("Failed to load mode:", error);
    }
  }

  async setMode(mode) {
    try {
      const response = await fetch("/mode", {
        method: "POST",
        body: mode,
      });
      if (response.ok) {
        this.currentMode = mode;
        this.updateModeUI();
        await this.resetRace();
      }
    } catch (error) {
      console.error("Failed to set mode:", error);
    }
  }

  updateModeUI() {
    this.modeRaceBtn.classList.toggle("active", this.currentMode === "race");
    this.modeLapBtn.classList.toggle("active", this.currentMode === "lap");

    const title = this.currentMode === "race" ? "Race Results" : "Lap Times";
    document.querySelector(".results-container h2").textContent = title;
  }

  updateRacerNames() {
    this.racers.forEach((racer) => {
      const nameEl = document.querySelector(
        `[data-racer="${racer.id}"] .racer-name`,
      );
      if (nameEl) nameEl.textContent = racer.name;
    });
  }

  editRacers() {
    const racersData = this.racers.map((r) => {
      const newName = prompt(`Enter name for Racer ${r.id}:`, r.name);
      return newName ? { id: r.id, name: newName } : r;
    });

    racersData.forEach(async (racer) => {
      try {
        await fetch("/racers", {
          method: "POST",
          body: JSON.stringify(racer),
        });
      } catch (error) {
        console.error("Failed to update racer:", error);
      }
    });

    this.loadRacers();
  }

  async startRace() {
    try {
      const response = await fetch("/start");
      if (response.ok) {
        this.raceActive = true;
        this.startTime = Date.now();
        this.startBtn.disabled = true;
        this.stopBtn.disabled = false;
        this.statusEl.textContent = "RACING";
        this.statusEl.style.color = "#00ff41";
        this.clearResults();
        this.startTimer();
      }
    } catch (error) {
      console.error("Failed to start race:", error);
      this.showError();
    }
  }

  async stopRace() {
    try {
      const response = await fetch("/stop");
      if (response.ok) {
        this.raceActive = false;
        this.startBtn.disabled = false;
        this.stopBtn.disabled = true;
        this.statusEl.textContent = "STOPPED";
        this.statusEl.style.color = "#ff0055";
        this.stopTimer();
      }
    } catch (error) {
      console.error("Failed to stop race:", error);
    }
  }

  async resetRace() {
    await this.stopRace();
    this.timerEl.textContent = "00:00.000";
    this.statusEl.textContent = "IDLE";
    this.statusEl.style.color = "#fff";
    this.clearResults();
    this.resetRacerCards();
  }

  startTimer() {
    this.timerInterval = setInterval(() => {
      const elapsed = Date.now() - this.startTime;
      this.timerEl.textContent = this.formatTime(elapsed);
    }, 10);
  }

  stopTimer() {
    if (this.timerInterval) {
      clearInterval(this.timerInterval);
      this.timerInterval = null;
    }
  }

  formatTime(ms) {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    const milliseconds = ms % 1000;

    return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}.${String(milliseconds).padStart(3, "0")}`;
  }

  async fetchResults() {
    try {
      const response = await fetch("/results");
      if (!response.ok) throw new Error("Failed to fetch results");

      const data = await response.json();
      this.updateResults(data);
      this.updateConnection(true);
      this.lastUpdateEl.textContent = new Date().toLocaleTimeString();
    } catch (error) {
      console.error("Failed to fetch results:", error);
      this.updateConnection(false);
    }
  }

  updateResults(results) {
    if (results.length === 0) {
      this.resultsEl.innerHTML =
        '<div class="no-results">No results yet...</div>';
      return;
    }

    if (this.currentMode === "race") {
      // Race mode - show positions
      this.resultsEl.innerHTML = results
        .sort((a, b) => a.position - b.position)
        .map((result) => {
          const medal =
            result.position === 1
              ? "🥇"
              : result.position === 2
                ? "🥈"
                : result.position === 3
                  ? "🥉"
                  : "";

          return `
                        <div class="result-item">
                            <div class="result-position">${medal || result.position}</div>
                            <div class="result-racer">${result.name}</div>
                            <div class="result-time">${this.formatTime(result.time)}</div>
                        </div>
                    `;
        })
        .join("");

      // Update racer cards
      results.forEach((result) => {
        this.updateRacerCard(result.racer, "finished", result.position);
      });
    } else {
      // Lap timer mode - show all laps
      this.resultsEl.innerHTML = results
        .sort((a, b) => b.timestamp - a.timestamp) // Most recent first
        .map((result, index) => {
          return `
                        <div class="result-item">
                            <div class="result-position">${results.length - index}</div>
                            <div class="result-racer">${result.name}</div>
                            <div class="result-time">
                                Lap: ${this.formatTime(result.lapTime)}<br>
                                <small>Total: ${this.formatTime(result.timestamp)}</small>
                            </div>
                        </div>
                    `;
        })
        .join("");
    }
  }

  updateRacerCard(racerId, status, position = null) {
    const card = document.querySelector(`[data-racer="${racerId}"]`);
    if (!card) return;

    card.classList.remove("active", "finished");

    const statusEl = card.querySelector(".racer-status");

    if (status === "finished") {
      card.classList.add("finished");
      statusEl.textContent = `P${position}`;
    } else if (status === "active") {
      card.classList.add("active");
      statusEl.textContent = "Racing";
    } else {
      statusEl.textContent = "Waiting";
    }
  }

  resetRacerCards() {
    document.querySelectorAll(".racer-card").forEach((card) => {
      card.classList.remove("active", "finished");
      card.querySelector(".racer-status").textContent = "Waiting";
    });
  }

  clearResults() {
    this.resultsEl.innerHTML =
      '<div class="no-results">Race in progress...</div>';
    this.resetRacerCards();
  }

  updateConnection(connected) {
    this.connectionEl.style.color = connected ? "#00ff41" : "#ff0055";
  }

  showError() {
    this.statusEl.textContent = "ERROR";
    this.statusEl.style.color = "#ff0055";
  }

  startPolling() {
    // Poll for results every 250ms
    this.updateInterval = setInterval(() => {
      this.fetchResults();
    }, 250);

    // Initial fetch
    this.fetchResults();
  }
}

// Initialize app when DOM is ready
document.addEventListener("DOMContentLoaded", () => {
  new RaceTimer();
});
