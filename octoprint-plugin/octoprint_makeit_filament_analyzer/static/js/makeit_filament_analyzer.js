$(function () {
    "use strict";

    const PLUGIN_ID = "makeit_filament_analyzer";
    const API_URL = API_BASEURL + "plugin/" + PLUGIN_ID;

    function asNumber(value) {
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function apiError(xhr) {
        if (xhr && xhr.responseJSON && xhr.responseJSON.error) return xhr.responseJSON.error;
        if (xhr && xhr.responseJSON && xhr.responseJSON.valid === false && xhr.responseJSON.error) {
            return xhr.responseJSON.error;
        }
        if (xhr && xhr.responseText) return xhr.responseText;
        return "Request failed";
    }

    function formatDate(unixSeconds) {
        if (!Number.isFinite(Number(unixSeconds))) return "";
        return new Date(Number(unixSeconds) * 1000).toLocaleString();
    }

    function MakeItFilamentAnalyzerViewModel() {
        const self = this;

        self.runName = ko.observable("Filament flow map");
        self.materialFamily = ko.observable("Custom");
        self.materialName = ko.observable("");
        self.manufacturer = ko.observable("");
        self.color = ko.observable("");
        self.lot = ko.observable("");
        self.notes = ko.observable("");
        self.printerName = ko.observable("MAKEiT test bench");
        self.extruderName = ko.observable("Dyze Pro");
        self.filamentDiameter = ko.observable(1.75);
        self.nozzleDiameter = ko.observable(0.6);
        self.nozzleMaterial = ko.observable("Brass");

        self.temperatureStart = ko.observable(180);
        self.temperatureEnd = ko.observable(220);
        self.temperatureStep = ko.observable(10);
        self.speedStart = ko.observable(300);
        self.speedEnd = ko.observable(600);
        self.speedStep = ko.observable(50);
        self.conditioningMm = ko.observable(50);
        self.measurementMm = ko.observable(200);
        self.settleSeconds = ko.observable(10);
        self.temperatureTolerance = ko.observable(3);

        self.recoveryTemp = ko.observable(220);
        self.recoveryPrimeMm = ko.observable(50);
        self.recoveryPrimeFeed = ko.observable(150);
        self.recoveryValidationMm = ko.observable(100);
        self.recoveryValidationFeed = ko.observable(150);
        self.recoveryAccuracy = ko.observable(97);

        self.accuracyThreshold = ko.observable(97);
        self.throughputThreshold = ko.observable(85);
        self.rollingWindow = ko.observable(50);
        self.rollingConfirm = ko.observable(2);
        self.encoderCalibration = ko.observable(0.685);
        self.segmentMm = ko.observable(0.35);
        self.maxInflight = ko.observable(2);
        self.reportMs = ko.observable(250);
        self.pulseGapFactor = ko.observable(4);
        self.pulseGapMinMs = ko.observable(500);
        self.pulseGapMissing = ko.observable(2);

        self.status = ko.observable("IDLE");
        self.phase = ko.observable("IDLE");
        self.error = ko.observable("");
        self.busy = ko.observable(false);
        self.preview = ko.observable(null);
        self.points = ko.observableArray([]);
        self.rows = ko.observableArray([]);
        self.recoveries = ko.observableArray([]);
        self.savedRuns = ko.observableArray([]);
        self.currentRunId = ko.observable(null);
        self.chartMetric = ko.observable("efficiency_pct");
        self.boundaryMetric = ko.observable("feed");
        self.activeTemperatureIndex = ko.observable(0);

        self.isActive = ko.pureComputed(function () {
            return ["STARTING", "RUNNING", "RECOVERING", "CANCELLING"].indexOf(self.status()) >= 0;
        });
        self.canStart = ko.pureComputed(function () {
            return Boolean(self.preview()) && !self.busy() && !self.isActive();
        });
        self.statusClass = ko.pureComputed(function () {
            return "status-" + String(self.status() || "idle").toLowerCase();
        });
        self.previewRows = ko.pureComputed(() => self.preview() ? self.preview().rows : 0);
        self.previewColumns = ko.pureComputed(() => self.preview() ? self.preview().columns : 0);
        self.previewPoints = ko.pureComputed(() => self.preview() ? self.preview().total_points : 0);
        self.previewFilament = ko.pureComputed(() => self.preview() ? self.preview().estimated_filament_mm + " mm" : "");
        self.previewMinutes = ko.pureComputed(() => self.preview() ? self.preview().estimated_minimum_minutes : "");
        self.previewCommandLength = ko.pureComputed(() => self.preview() ? self.preview().row_gcode_length : "");
        self.previewRecoveryLength = ko.pureComputed(() => self.preview() ? self.preview().recovery_gcode_length : "");
        self.previewGcode = ko.pureComputed(() => self.preview() ? self.preview().gcode : "");
        self.progressText = ko.pureComputed(function () {
            const preview = self.preview();
            if (!preview) return "";
            const done = self.points().length;
            const total = preview.total_points || 0;
            const row = Math.min(self.activeTemperatureIndex() + 1, preview.rows || 0);
            return done + " measured points; temperature row " + row + " / " + (preview.rows || 0) + "; maximum " + total + " points";
        });

        self.toDefinition = function () {
            return {
                run_name: self.runName(),
                material_family: self.materialFamily(),
                material_name: self.materialName(),
                manufacturer: self.manufacturer(),
                color: self.color(),
                lot: self.lot(),
                notes: self.notes(),
                printer_name: self.printerName(),
                extruder_name: self.extruderName(),
                filament_diameter_mm: asNumber(self.filamentDiameter()),
                nozzle_diameter_mm: asNumber(self.nozzleDiameter()),
                nozzle_material: self.nozzleMaterial(),
                temperature_start_c: asNumber(self.temperatureStart()),
                temperature_end_c: asNumber(self.temperatureEnd()),
                temperature_step_c: asNumber(self.temperatureStep()),
                speed_start_mm_min: asNumber(self.speedStart()),
                speed_end_mm_min: asNumber(self.speedEnd()),
                speed_step_mm_min: asNumber(self.speedStep()),
                conditioning_mm: asNumber(self.conditioningMm()),
                measurement_mm: asNumber(self.measurementMm()),
                settle_seconds: asNumber(self.settleSeconds()),
                recovery_temp_c: asNumber(self.recoveryTemp()),
                recovery_prime_mm: asNumber(self.recoveryPrimeMm()),
                recovery_prime_feed_mm_min: asNumber(self.recoveryPrimeFeed()),
                recovery_validation_mm: asNumber(self.recoveryValidationMm()),
                recovery_validation_feed_mm_min: asNumber(self.recoveryValidationFeed()),
                recovery_accuracy_threshold_pct: asNumber(self.recoveryAccuracy()),
                accuracy_threshold_pct: asNumber(self.accuracyThreshold()),
                throughput_threshold_pct: asNumber(self.throughputThreshold()),
                temperature_tolerance_c: asNumber(self.temperatureTolerance()),
                rolling_window_mm: asNumber(self.rollingWindow()),
                rolling_confirm_windows: asNumber(self.rollingConfirm()),
                encoder_events_per_mm: asNumber(self.encoderCalibration()),
                segment_mm: asNumber(self.segmentMm()),
                max_inflight: asNumber(self.maxInflight()),
                report_ms: asNumber(self.reportMs()),
                pulse_gap_factor: asNumber(self.pulseGapFactor()),
                pulse_gap_min_ms: asNumber(self.pulseGapMinMs()),
                pulse_gap_missing_events: asNumber(self.pulseGapMissing())
            };
        };

        self.applyDefinition = function (d) {
            if (!d) return;
            const mapping = [
                ["runName", "run_name"], ["materialFamily", "material_family"],
                ["materialName", "material_name"], ["manufacturer", "manufacturer"],
                ["color", "color"], ["lot", "lot"], ["notes", "notes"],
                ["printerName", "printer_name"], ["extruderName", "extruder_name"],
                ["filamentDiameter", "filament_diameter_mm"], ["nozzleDiameter", "nozzle_diameter_mm"],
                ["nozzleMaterial", "nozzle_material"], ["temperatureStart", "temperature_start_c"],
                ["temperatureEnd", "temperature_end_c"], ["temperatureStep", "temperature_step_c"],
                ["speedStart", "speed_start_mm_min"], ["speedEnd", "speed_end_mm_min"],
                ["speedStep", "speed_step_mm_min"], ["conditioningMm", "conditioning_mm"],
                ["measurementMm", "measurement_mm"], ["settleSeconds", "settle_seconds"],
                ["temperatureTolerance", "temperature_tolerance_c"], ["recoveryTemp", "recovery_temp_c"],
                ["recoveryPrimeMm", "recovery_prime_mm"], ["recoveryPrimeFeed", "recovery_prime_feed_mm_min"],
                ["recoveryValidationMm", "recovery_validation_mm"],
                ["recoveryValidationFeed", "recovery_validation_feed_mm_min"],
                ["recoveryAccuracy", "recovery_accuracy_threshold_pct"],
                ["accuracyThreshold", "accuracy_threshold_pct"],
                ["throughputThreshold", "throughput_threshold_pct"],
                ["rollingWindow", "rolling_window_mm"], ["rollingConfirm", "rolling_confirm_windows"],
                ["encoderCalibration", "encoder_events_per_mm"], ["segmentMm", "segment_mm"],
                ["maxInflight", "max_inflight"], ["reportMs", "report_ms"],
                ["pulseGapFactor", "pulse_gap_factor"], ["pulseGapMinMs", "pulse_gap_min_ms"],
                ["pulseGapMissing", "pulse_gap_missing_events"]
            ];
            mapping.forEach(function (entry) {
                if (Object.prototype.hasOwnProperty.call(d, entry[1]) && typeof self[entry[0]] === "function") {
                    self[entry[0]](d[entry[1]]);
                }
            });
        };

        self.validateGrid = function () {
            self.busy(true);
            self.error("");
            OctoPrint.simpleApiCommand(PLUGIN_ID, "validate", {definition: self.toDefinition()})
                .done(function (response) {
                    self.preview(response.preview);
                    self.drawAllCharts();
                })
                .fail(function (xhr) {
                    self.preview(null);
                    self.error(apiError(xhr));
                })
                .always(function () { self.busy(false); });
        };

        self.startRun = function () {
            const preview = self.preview();
            if (!preview) return;
            if (preview.requires_filament_confirmation) {
                const message = "This run may use up to approximately " +
                    preview.estimated_filament_mm + " mm of filament. Start the test?";
                if (!window.confirm(message)) return;
            }
            self.busy(true);
            self.error("");
            OctoPrint.simpleApiCommand(PLUGIN_ID, "start", {definition: self.toDefinition()})
                .done(function (response) {
                    self.points([]);
                    self.rows([]);
                    self.recoveries([]);
                    self.currentRunId(response.run_id);
                    self.status("RUNNING");
                    self.phase("STARTING");
                    self.drawAllCharts();
                })
                .fail(function (xhr) { self.error(apiError(xhr)); })
                .always(function () { self.busy(false); });
        };

        self.cancelRun = function () {
            if (!window.confirm("Stop the analyzer safely and turn off the hotend?")) return;
            self.busy(true);
            self.error("");
            OctoPrint.simpleApiCommand(PLUGIN_ID, "cancel", {})
                .done(function () {
                    self.status("CANCELLING");
                    self.phase("CANCELLING");
                })
                .fail(function (xhr) { self.error(apiError(xhr)); })
                .always(function () { self.busy(false); });
        };

        self.clearRun = function () {
            self.busy(true);
            OctoPrint.simpleApiCommand(PLUGIN_ID, "clear", {})
                .done(function () {
                    self.status("IDLE");
                    self.phase("IDLE");
                    self.currentRunId(null);
                    self.points([]);
                    self.rows([]);
                    self.recoveries([]);
                    self.error("");
                    self.drawAllCharts();
                })
                .fail(function (xhr) { self.error(apiError(xhr)); })
                .always(function () { self.busy(false); });
        };

        self.loadSavedRuns = function () {
            $.getJSON(API_URL, {action: "runs"})
                .done(function (response) {
                    self.savedRuns(Array.isArray(response.runs) ? response.runs : []);
                })
                .fail(function (xhr) { self.error(apiError(xhr)); });
        };

        self.loadSavedRun = function (run) {
            if (!run || !run.run_id) return;
            $.getJSON(API_URL, {action: "load", run_id: run.run_id})
                .done(function (state) {
                    self.applyDefinition(state.definition || {});
                    self.applyState(state);
                    self.currentRunId(run.run_id);
                })
                .fail(function (xhr) { self.error(apiError(xhr)); });
        };

        self.deleteSavedRun = function (run) {
            if (!run || !run.run_id) return;
            if (!window.confirm("Delete saved run " + run.run_name + "?")) return;
            OctoPrint.simpleApiCommand(PLUGIN_ID, "delete_run", {run_id: run.run_id})
                .done(function (response) {
                    self.savedRuns(Array.isArray(response.runs) ? response.runs : []);
                })
                .fail(function (xhr) { self.error(apiError(xhr)); });
        };

        self.csvUrl = function (run) {
            return run && run.csv_url ? run.csv_url : "#";
        };
        self.jsonUrl = function (run) {
            return run && run.json_url ? run.json_url : "#";
        };
        self.savedRunDate = function (run) {
            return formatDate(run && run.started_at);
        };

        self.applyState = function (state) {
            if (!state) return;
            self.status(state.status || "IDLE");
            self.phase(state.phase || "IDLE");
            self.currentRunId(state.run_id || null);
            self.activeTemperatureIndex(Number(state.temperature_index || 0));
            self.points(Array.isArray(state.points) ? state.points : []);
            self.rows(Array.isArray(state.rows) ? state.rows : []);
            self.recoveries(Array.isArray(state.recoveries) ? state.recoveries : []);
            self.error(state.error || "");
            if (state.preview) self.preview(state.preview);
            self.drawAllCharts();
        };

        self.onStartupComplete = function () {
            OctoPrint.simpleApiGet(PLUGIN_ID)
                .done(self.applyState)
                .fail(function (xhr) { self.error(apiError(xhr)); });
            self.loadSavedRuns();
        };

        self.onDataUpdaterPluginMessage = function (plugin, message) {
            if (plugin !== PLUGIN_ID || !message) return;
            if (message.type === "state") {
                self.applyState(message.state);
                if (!self.isActive()) self.loadSavedRuns();
                return;
            }
            if (message.point) {
                const points = self.points().slice();
                points.push(message.point);
                self.points(points);
                self.drawAllCharts();
            }
            if (message.row) {
                const rows = self.rows().slice();
                rows.push(message.row);
                self.rows(rows);
                self.drawBoundaryChart();
            }
            if (message.recovery) {
                const recoveries = self.recoveries().slice();
                recoveries.push(message.recovery);
                self.recoveries(recoveries);
            }
        };

        self.onTabChange = function (current) {
            if (current === "#tab_plugin_makeit_filament_analyzer") {
                window.setTimeout(self.drawAllCharts, 0);
            }
        };

        const definitionInputs = [
            self.runName, self.materialFamily, self.materialName, self.manufacturer,
            self.color, self.lot, self.notes, self.printerName, self.extruderName,
            self.filamentDiameter, self.nozzleDiameter, self.nozzleMaterial,
            self.temperatureStart, self.temperatureEnd, self.temperatureStep,
            self.speedStart, self.speedEnd, self.speedStep, self.conditioningMm,
            self.measurementMm, self.settleSeconds, self.temperatureTolerance,
            self.recoveryTemp, self.recoveryPrimeMm, self.recoveryPrimeFeed,
            self.recoveryValidationMm, self.recoveryValidationFeed, self.recoveryAccuracy,
            self.accuracyThreshold, self.throughputThreshold, self.rollingWindow,
            self.rollingConfirm, self.encoderCalibration, self.segmentMm,
            self.maxInflight, self.reportMs, self.pulseGapFactor,
            self.pulseGapMinMs, self.pulseGapMissing
        ];
        definitionInputs.forEach(function (observable) {
            observable.subscribe(function () {
                if (!self.isActive()) self.preview(null);
            });
        });

        self.chartMetric.subscribe(function () { self.drawHeatmap(); });
        self.boundaryMetric.subscribe(function () { self.drawBoundaryChart(); });

        self.drawAllCharts = function () {
            self.drawHeatmap();
            self.drawBoundaryChart();
        };

        self.drawHeatmap = function () {
            const canvas = document.getElementById("makeit-fa-heatmap");
            if (!canvas) return;
            const ctx = canvas.getContext("2d");
            const preview = self.preview();
            const points = self.points();
            const temperatures = preview && preview.temperatures ? preview.temperatures.slice() :
                Array.from(new Set(points.map(p => Number(p.temperature_c)))).sort((a, b) => a - b);
            const speeds = preview && preview.speeds_mm_min ? preview.speeds_mm_min.slice() :
                Array.from(new Set(points.map(p => Number(p.feed_mm_min)))).sort((a, b) => a - b);

            const cssWidth = Math.max(720, canvas.parentElement ? canvas.parentElement.clientWidth - 20 : 980);
            const cssHeight = Math.max(420, Math.min(780, 180 + speeds.length * 32));
            const scale = window.devicePixelRatio || 1;
            canvas.style.width = cssWidth + "px";
            canvas.style.height = cssHeight + "px";
            canvas.width = Math.floor(cssWidth * scale);
            canvas.height = Math.floor(cssHeight * scale);
            ctx.setTransform(scale, 0, 0, scale, 0, 0);
            ctx.clearRect(0, 0, cssWidth, cssHeight);
            ctx.fillStyle = "#ffffff";
            ctx.fillRect(0, 0, cssWidth, cssHeight);

            if (!temperatures.length || !speeds.length) {
                ctx.fillStyle = "#777";
                ctx.font = "14px sans-serif";
                ctx.fillText("Validate a grid or load a run to display the heatmap.", 24, 40);
                return;
            }

            const margin = {left: 100, right: 28, top: 38, bottom: 70};
            const plotWidth = cssWidth - margin.left - margin.right;
            const plotHeight = cssHeight - margin.top - margin.bottom;
            const cellWidth = plotWidth / temperatures.length;
            const cellHeight = plotHeight / speeds.length;
            const metric = self.chartMetric();
            const pointMap = new Map();
            points.forEach(function (point) {
                pointMap.set(Number(point.temperature_c) + "|" + Number(point.feed_mm_min), point);
            });

            const numericValues = points.map(p => Number(p[metric])).filter(Number.isFinite);
            const minValue = numericValues.length ? Math.min.apply(null, numericValues) : 0;
            const maxValue = numericValues.length ? Math.max.apply(null, numericValues) : 1;

            function continuousColor(value) {
                if (!Number.isFinite(value)) return "#dedede";
                const span = Math.max(1e-9, maxValue - minValue);
                const ratio = Math.max(0, Math.min(1, (value - minValue) / span));
                const hue = 220 - ratio * 180;
                return "hsl(" + hue + ", 70%, 52%)";
            }

            function cellColor(point) {
                if (!point) return "#e3e3e3";
                if (metric !== "efficiency_pct") return continuousColor(Number(point[metric]));
                if (point.classification === "ACCURATE") return "#4caf50";
                if (point.classification === "BELOW_ACCURACY_ABOVE_THROUGHPUT") return "#f2a900";
                if (point.classification === "INVALID_TEMP") return "#8e6bbd";
                if (point.classification === "ABORTED") return "#808080";
                return "#d9534f";
            }

            ctx.font = "12px sans-serif";
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            temperatures.forEach(function (temperature, xIndex) {
                speeds.forEach(function (speed, speedIndex) {
                    const yIndex = speeds.length - 1 - speedIndex;
                    const x = margin.left + xIndex * cellWidth;
                    const y = margin.top + yIndex * cellHeight;
                    const point = pointMap.get(Number(temperature) + "|" + Number(speed));
                    ctx.fillStyle = cellColor(point);
                    ctx.fillRect(x + 1, y + 1, Math.max(1, cellWidth - 2), Math.max(1, cellHeight - 2));
                    if (point && cellWidth >= 38 && cellHeight >= 22) {
                        const value = Number(point[metric]);
                        ctx.fillStyle = "#111";
                        const label = Number.isFinite(value) ?
                            value.toFixed(metric === "efficiency_pct" ? 1 : 2) : "—";
                        ctx.fillText(label, x + cellWidth / 2, y + cellHeight / 2);
                    }
                });
            });

            ctx.fillStyle = "#222";
            const xTickEvery = Math.max(1, Math.ceil(temperatures.length / 18));
            temperatures.forEach(function (temperature, index) {
                if (index % xTickEvery !== 0 && index !== temperatures.length - 1) return;
                ctx.fillText(String(temperature), margin.left + (index + 0.5) * cellWidth, cssHeight - margin.bottom + 20);
            });
            ctx.fillText("Temperature, °C", margin.left + plotWidth / 2, cssHeight - 20);

            ctx.textAlign = "right";
            const yTickEvery = Math.max(1, Math.ceil(speeds.length / 18));
            speeds.forEach(function (speed, index) {
                if (index % yTickEvery !== 0 && index !== speeds.length - 1) return;
                const yIndex = speeds.length - 1 - index;
                ctx.fillText(String(speed), margin.left - 8, margin.top + (yIndex + 0.5) * cellHeight);
            });
            ctx.save();
            ctx.translate(18, margin.top + plotHeight / 2);
            ctx.rotate(-Math.PI / 2);
            ctx.textAlign = "center";
            ctx.fillText("Filament feed, mm/min", 0, 0);
            ctx.restore();
        };

        self.drawBoundaryChart = function () {
            const canvas = document.getElementById("makeit-fa-boundary");
            if (!canvas) return;
            const ctx = canvas.getContext("2d");
            const rows = self.rows().slice().sort((a, b) => Number(a.temperature_c) - Number(b.temperature_c));
            const cssWidth = Math.max(640, canvas.parentElement ? canvas.parentElement.clientWidth - 20 : 900);
            const cssHeight = 360;
            const scale = window.devicePixelRatio || 1;
            canvas.style.width = cssWidth + "px";
            canvas.style.height = cssHeight + "px";
            canvas.width = Math.floor(cssWidth * scale);
            canvas.height = Math.floor(cssHeight * scale);
            ctx.setTransform(scale, 0, 0, scale, 0, 0);
            ctx.clearRect(0, 0, cssWidth, cssHeight);
            ctx.fillStyle = "#fff";
            ctx.fillRect(0, 0, cssWidth, cssHeight);

            if (!rows.length) {
                ctx.fillStyle = "#777";
                ctx.font = "14px sans-serif";
                ctx.fillText("Completed temperature rows will appear here.", 24, 40);
                return;
            }

            const d = self.toDefinition();
            const flowMode = self.boundaryMetric() === "flow";
            const valueForFeed = function (feed) {
                const number = Number(feed);
                if (!Number.isFinite(number) || number <= 0) return null;
                return flowMode ? number * Math.PI * d.filament_diameter_mm * d.filament_diameter_mm / 240 : number;
            };
            const series = [
                {
                    label: "Accurate",
                    color: "#4caf50",
                    values: rows.map(r => valueForFeed(r.accuracy_last_pass || r.last_accurate_feed_mm_min))
                },
                {
                    label: "Hard throughput",
                    color: "#d98200",
                    values: rows.map(r => valueForFeed(r.last_pass))
                }
            ];
            const allValues = series.flatMap(s => s.values).filter(Number.isFinite);
            if (!allValues.length) {
                ctx.fillStyle = "#777";
                ctx.fillText("No positive boundary values were reported in these rows.", 24, 40);
                return;
            }

            const temperatures = rows.map(r => Number(r.temperature_c));
            const xMin = Math.min.apply(null, temperatures);
            const xMax = Math.max.apply(null, temperatures);
            const yMin = 0;
            const yMax = Math.max.apply(null, allValues) * 1.1;
            const margin = {left: 72, right: 24, top: 36, bottom: 58};
            const plotWidth = cssWidth - margin.left - margin.right;
            const plotHeight = cssHeight - margin.top - margin.bottom;
            const xFor = x => margin.left + (xMax === xMin ? 0.5 : (x - xMin) / (xMax - xMin)) * plotWidth;
            const yFor = y => margin.top + plotHeight - (y - yMin) / Math.max(1e-9, yMax - yMin) * plotHeight;

            ctx.strokeStyle = "#ddd";
            ctx.fillStyle = "#333";
            ctx.font = "12px sans-serif";
            ctx.textAlign = "right";
            for (let i = 0; i <= 5; i++) {
                const value = yMax * i / 5;
                const y = yFor(value);
                ctx.beginPath();
                ctx.moveTo(margin.left, y);
                ctx.lineTo(cssWidth - margin.right, y);
                ctx.stroke();
                ctx.fillText(value.toFixed(flowMode ? 1 : 0), margin.left - 8, y);
            }

            ctx.textAlign = "center";
            temperatures.forEach(function (temperature) {
                ctx.fillText(String(temperature), xFor(temperature), cssHeight - margin.bottom + 20);
            });
            ctx.fillText("Temperature, °C", margin.left + plotWidth / 2, cssHeight - 18);

            series.forEach(function (s) {
                ctx.strokeStyle = s.color;
                ctx.fillStyle = s.color;
                ctx.lineWidth = 2;
                let started = false;
                ctx.beginPath();
                s.values.forEach(function (value, index) {
                    if (!Number.isFinite(value)) return;
                    const x = xFor(temperatures[index]);
                    const y = yFor(value);
                    if (!started) {
                        ctx.moveTo(x, y);
                        started = true;
                    } else {
                        ctx.lineTo(x, y);
                    }
                });
                if (started) ctx.stroke();
                s.values.forEach(function (value, index) {
                    if (!Number.isFinite(value)) return;
                    const x = xFor(temperatures[index]);
                    const y = yFor(value);
                    ctx.beginPath();
                    ctx.arc(x, y, 4, 0, Math.PI * 2);
                    ctx.fill();
                });
            });

            ctx.textAlign = "left";
            series.forEach(function (s, index) {
                const x = margin.left + index * 150;
                ctx.fillStyle = s.color;
                ctx.fillRect(x, 12, 18, 4);
                ctx.fillStyle = "#333";
                ctx.fillText(s.label, x + 24, 14);
            });

            ctx.save();
            ctx.translate(18, margin.top + plotHeight / 2);
            ctx.rotate(-Math.PI / 2);
            ctx.textAlign = "center";
            ctx.fillStyle = "#333";
            ctx.fillText(flowMode ? "Volumetric flow, mm³/s" : "Filament feed, mm/min", 0, 0);
            ctx.restore();
        };
    }

    OCTOPRINT_VIEWMODELS.push([
        MakeItFilamentAnalyzerViewModel,
        [],
        ["#tab_plugin_makeit_filament_analyzer"]
    ]);
});
