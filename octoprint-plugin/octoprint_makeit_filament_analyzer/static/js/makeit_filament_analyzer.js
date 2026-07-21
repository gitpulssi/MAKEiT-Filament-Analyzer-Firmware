$(function () {
    "use strict";

    const PLUGIN_ID = "makeit_filament_analyzer";

    function asNumber(value) {
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function apiError(xhr) {
        if (xhr && xhr.responseJSON && xhr.responseJSON.error) return xhr.responseJSON.error;
        if (xhr && xhr.responseText) return xhr.responseText;
        return "Request failed";
    }

    function MakeItFilamentAnalyzerViewModel() {
        const self = this;

        self.runName = ko.observable("Filament flow map");
        self.materialFamily = ko.observable("PLA");
        self.materialName = ko.observable("");
        self.manufacturer = ko.observable("");
        self.lot = ko.observable("");
        self.notes = ko.observable("");
        self.filamentDiameter = ko.observable(1.75);
        self.nozzleDiameter = ko.observable(0.6);

        self.temperatureStart = ko.observable(180);
        self.temperatureEnd = ko.observable(220);
        self.temperatureStep = ko.observable(10);
        self.speedStart = ko.observable(300);
        self.speedEnd = ko.observable(600);
        self.speedStep = ko.observable(50);
        self.conditioningMm = ko.observable(50);
        self.measurementMm = ko.observable(200);
        self.settleSeconds = ko.observable(10);
        self.recoveryTemp = ko.observable(220);
        self.temperatureTolerance = ko.observable(3);

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
        self.error = ko.observable("");
        self.busy = ko.observable(false);
        self.preview = ko.observable(null);
        self.points = ko.observableArray([]);
        self.rows = ko.observableArray([]);
        self.chartMetric = ko.observable("efficiency_pct");

        self.isActive = ko.pureComputed(function () {
            return ["STARTING", "RUNNING", "CANCELLING"].indexOf(self.status()) >= 0;
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
        self.previewCommandLength = ko.pureComputed(() => self.preview() ? self.preview().gcode_length : "");
        self.previewGcode = ko.pureComputed(() => self.preview() ? self.preview().gcode : "");

        self.toDefinition = function () {
            return {
                run_name: self.runName(),
                material_family: self.materialFamily(),
                material_name: self.materialName(),
                manufacturer: self.manufacturer(),
                lot: self.lot(),
                notes: self.notes(),
                filament_diameter_mm: asNumber(self.filamentDiameter()),
                nozzle_diameter_mm: asNumber(self.nozzleDiameter()),
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

        self.validateGrid = function () {
            self.busy(true);
            self.error("");
            OctoPrint.simpleApiCommand(PLUGIN_ID, "validate", {definition: self.toDefinition()})
                .done(function (response) {
                    self.preview(response.preview);
                    self.drawHeatmap();
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
                const message = "This run may use approximately " + preview.estimated_filament_mm +
                    " mm of filament. Start the test?";
                if (!window.confirm(message)) return;
            }
            self.busy(true);
            self.error("");
            OctoPrint.simpleApiCommand(PLUGIN_ID, "start", {definition: self.toDefinition()})
                .done(function () {
                    self.points([]);
                    self.rows([]);
                    self.status("RUNNING");
                    self.drawHeatmap();
                })
                .fail(function (xhr) { self.error(apiError(xhr)); })
                .always(function () { self.busy(false); });
        };

        self.cancelRun = function () {
            self.busy(true);
            self.error("");
            OctoPrint.simpleApiCommand(PLUGIN_ID, "cancel", {})
                .done(function () { self.status("CANCELLING"); })
                .fail(function (xhr) { self.error(apiError(xhr)); })
                .always(function () { self.busy(false); });
        };

        self.clearRun = function () {
            self.busy(true);
            OctoPrint.simpleApiCommand(PLUGIN_ID, "clear", {})
                .done(function () {
                    self.status("IDLE");
                    self.points([]);
                    self.rows([]);
                    self.error("");
                    self.drawHeatmap();
                })
                .fail(function (xhr) { self.error(apiError(xhr)); })
                .always(function () { self.busy(false); });
        };

        self.applyState = function (state) {
            if (!state) return;
            self.status(state.status || "IDLE");
            self.points(Array.isArray(state.points) ? state.points : []);
            self.rows(Array.isArray(state.rows) ? state.rows : []);
            self.error(state.error || "");
            if (state.preview) self.preview(state.preview);
            self.drawHeatmap();
        };

        self.onStartupComplete = function () {
            OctoPrint.simpleApiGet(PLUGIN_ID)
                .done(self.applyState)
                .fail(function (xhr) { self.error(apiError(xhr)); });
        };

        self.onDataUpdaterPluginMessage = function (plugin, message) {
            if (plugin !== PLUGIN_ID || !message) return;
            if (message.type === "state") {
                self.applyState(message.state);
                return;
            }
            if (message.point) {
                const points = self.points().slice();
                points.push(message.point);
                self.points(points);
                self.drawHeatmap();
            }
            if (message.terminal) {
                self.status(message.terminal.state || "TERMINAL");
            }
        };

        self.onTabChange = function (current, previous) {
            if (current === "#tab_plugin_makeit_filament_analyzer") {
                window.setTimeout(self.drawHeatmap, 0);
            }
        };

        self.chartMetric.subscribe(function () { self.drawHeatmap(); });

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

            const cssWidth = Math.max(640, canvas.parentElement ? canvas.parentElement.clientWidth - 20 : 900);
            const cssHeight = Math.max(420, Math.min(680, 180 + speeds.length * 38));
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
                ctx.fillText("Validate a grid or start a run to display the heatmap.", 24, 40);
                return;
            }

            const margin = {left: 92, right: 24, top: 36, bottom: 62};
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
                    if (point) {
                        const value = Number(point[metric]);
                        ctx.fillStyle = "#111";
                        const label = Number.isFinite(value) ? value.toFixed(metric === "efficiency_pct" ? 1 : 2) : "—";
                        if (cellWidth >= 42 && cellHeight >= 24) ctx.fillText(label, x + cellWidth / 2, y + cellHeight / 2);
                    }
                });
            });

            ctx.fillStyle = "#222";
            ctx.textAlign = "center";
            temperatures.forEach(function (temperature, index) {
                ctx.fillText(String(temperature), margin.left + (index + 0.5) * cellWidth, cssHeight - margin.bottom + 20);
            });
            ctx.fillText("Temperature, °C", margin.left + plotWidth / 2, cssHeight - 18);

            ctx.textAlign = "right";
            speeds.forEach(function (speed, index) {
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
    }

    OCTOPRINT_VIEWMODELS.push([
        MakeItFilamentAnalyzerViewModel,
        [],
        ["#tab_plugin_makeit_filament_analyzer"]
    ]);
});
