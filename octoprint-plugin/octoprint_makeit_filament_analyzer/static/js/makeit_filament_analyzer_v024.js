$(function () {
    "use strict";

    const TAB_SELECTOR = "#tab_plugin_makeit_filament_analyzer";

    function numberOr(value, fallback) {
        const number = Number(value);
        return Number.isFinite(number) ? number : fallback;
    }

    function registrationUsesAnalyzerTab(registration) {
        if (!Array.isArray(registration) || !Array.isArray(registration[2])) return false;
        return registration[2].indexOf(TAB_SELECTOR) >= 0;
    }

    const registration = OCTOPRINT_VIEWMODELS.find(registrationUsesAnalyzerTab);
    if (!registration) {
        console.error("MAKEiT Filament Analyzer 0.2.4 could not find its base view model");
        return;
    }

    const BaseViewModel = registration[0];
    if (BaseViewModel.__makeitFaPrintSpeedV024) return;

    function MakeItFilamentAnalyzerPrintSpeedViewModel() {
        BaseViewModel.apply(this, arguments);
        const self = this;

        const initialNozzle = Math.max(0.05, numberOr(self.nozzleDiameter(), 0.6));
        self.printLineWidth = ko.observable(initialNozzle);
        self.printLayerHeight = ko.observable(initialNozzle * 0.5);
        self.printSpeedSafety = ko.observable(90);

        self.useNozzlePrintGeometry = function () {
            const nozzle = Math.max(0.05, numberOr(self.nozzleDiameter(), 0.6));
            self.printLineWidth(Number(nozzle.toFixed(3)));
            self.printLayerHeight(Number((nozzle * 0.5).toFixed(3)));
        };

        self.printGeometryText = ko.pureComputed(function () {
            const width = numberOr(self.printLineWidth(), 0);
            const height = numberOr(self.printLayerHeight(), 0);
            const safety = numberOr(self.printSpeedSafety(), 0);
            return "Speed = delivered flow x " + safety.toFixed(0) +
                "% / (" + width.toFixed(3) + " mm x " + height.toFixed(3) + " mm)";
        });

        const baseToDefinition = self.toDefinition;
        self.toDefinition = function () {
            const definition = baseToDefinition();
            definition.print_line_width_mm = numberOr(self.printLineWidth(), null);
            definition.print_layer_height_mm = numberOr(self.printLayerHeight(), null);
            definition.print_speed_safety_pct = numberOr(self.printSpeedSafety(), null);
            return definition;
        };

        const baseApplyDefinition = self.applyDefinition;
        self.applyDefinition = function (definition) {
            baseApplyDefinition(definition);
            const nozzle = Math.max(0.05, numberOr(self.nozzleDiameter(), 0.6));
            self.printLineWidth(numberOr(definition && definition.print_line_width_mm, nozzle));
            self.printLayerHeight(numberOr(
                definition && definition.print_layer_height_mm,
                nozzle * 0.5
            ));
            self.printSpeedSafety(numberOr(
                definition && definition.print_speed_safety_pct,
                90
            ));
        };

        self.recommendedPrintSpeed = function (point) {
            const flow = numberOr(point && point.delivered_flow_mm3_s, NaN);
            const width = numberOr(self.printLineWidth(), NaN);
            const height = numberOr(self.printLayerHeight(), NaN);
            const safety = numberOr(self.printSpeedSafety(), NaN);
            const area = width * height;
            if (!Number.isFinite(flow) || !Number.isFinite(area) || area <= 0 ||
                    !Number.isFinite(safety) || safety <= 0) return null;
            return flow * safety / 100 / area;
        };

        self.drawPrintSpeedMap = function () {
            const canvas = document.getElementById("makeit-fa-print-speed-map");
            if (!canvas) return;
            const ctx = canvas.getContext("2d");
            const preview = self.preview();
            const points = self.points();
            const temperatures = preview && preview.temperatures ? preview.temperatures.slice() :
                Array.from(new Set(points.map(p => Number(p.temperature_c)))).sort((a, b) => a - b);
            const feeds = preview && preview.speeds_mm_min ? preview.speeds_mm_min.slice() :
                Array.from(new Set(points.map(p => Number(p.feed_mm_min)))).sort((a, b) => a - b);

            const cssWidth = Math.max(
                720,
                canvas.parentElement ? canvas.parentElement.clientWidth - 20 : 980
            );
            const cssHeight = Math.max(420, Math.min(780, 180 + feeds.length * 32));
            const scale = window.devicePixelRatio || 1;
            canvas.style.width = cssWidth + "px";
            canvas.style.height = cssHeight + "px";
            canvas.width = Math.floor(cssWidth * scale);
            canvas.height = Math.floor(cssHeight * scale);
            ctx.setTransform(scale, 0, 0, scale, 0, 0);
            ctx.clearRect(0, 0, cssWidth, cssHeight);
            ctx.fillStyle = "#ffffff";
            ctx.fillRect(0, 0, cssWidth, cssHeight);

            if (!temperatures.length || !feeds.length) {
                ctx.fillStyle = "#777";
                ctx.font = "14px sans-serif";
                ctx.fillText("Validate a grid or load a run to display print speed.", 24, 40);
                return;
            }

            const margin = {left: 100, right: 28, top: 38, bottom: 70};
            const plotWidth = cssWidth - margin.left - margin.right;
            const plotHeight = cssHeight - margin.top - margin.bottom;
            const cellWidth = plotWidth / temperatures.length;
            const cellHeight = plotHeight / feeds.length;
            const pointMap = new Map();
            points.forEach(function (point) {
                pointMap.set(Number(point.temperature_c) + "|" + Number(point.feed_mm_min), point);
            });

            function cellColor(point) {
                if (!point) return "#e3e3e3";
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
                feeds.forEach(function (feed, feedIndex) {
                    const yIndex = feeds.length - 1 - feedIndex;
                    const x = margin.left + xIndex * cellWidth;
                    const y = margin.top + yIndex * cellHeight;
                    const point = pointMap.get(Number(temperature) + "|" + Number(feed));
                    ctx.fillStyle = cellColor(point);
                    ctx.fillRect(
                        x + 1,
                        y + 1,
                        Math.max(1, cellWidth - 2),
                        Math.max(1, cellHeight - 2)
                    );
                    if (point && cellWidth >= 38 && cellHeight >= 22) {
                        const speed = self.recommendedPrintSpeed(point);
                        ctx.fillStyle = "#111";
                        ctx.fillText(
                            Number.isFinite(speed) ? speed.toFixed(speed >= 100 ? 0 : 1) : "-",
                            x + cellWidth / 2,
                            y + cellHeight / 2
                        );
                    }
                });
            });

            ctx.fillStyle = "#222";
            const xTickEvery = Math.max(1, Math.ceil(temperatures.length / 18));
            temperatures.forEach(function (temperature, index) {
                if (index % xTickEvery !== 0 && index !== temperatures.length - 1) return;
                ctx.fillText(
                    String(temperature),
                    margin.left + (index + 0.5) * cellWidth,
                    cssHeight - margin.bottom + 20
                );
            });
            ctx.fillText("Temperature, C", margin.left + plotWidth / 2, cssHeight - 20);

            ctx.textAlign = "right";
            const yTickEvery = Math.max(1, Math.ceil(feeds.length / 18));
            feeds.forEach(function (feed, index) {
                if (index % yTickEvery !== 0 && index !== feeds.length - 1) return;
                const yIndex = feeds.length - 1 - index;
                ctx.fillText(
                    String(feed),
                    margin.left - 8,
                    margin.top + (yIndex + 0.5) * cellHeight
                );
            });
            ctx.save();
            ctx.translate(18, margin.top + plotHeight / 2);
            ctx.rotate(-Math.PI / 2);
            ctx.textAlign = "center";
            ctx.fillText("Filament feed, mm/min", 0, 0);
            ctx.restore();

            ctx.textAlign = "left";
            ctx.fillStyle = "#333";
            ctx.font = "12px sans-serif";
            ctx.fillText("Cell value: recommended linear print speed, mm/s", margin.left, 18);
        };

        const baseDrawAllCharts = self.drawAllCharts;
        self.drawAllCharts = function () {
            baseDrawAllCharts();
            self.drawPrintSpeedMap();
        };

        [self.printLineWidth, self.printLayerHeight, self.printSpeedSafety].forEach(
            function (observable) {
                observable.subscribe(function () {
                    self.drawPrintSpeedMap();
                });
            }
        );

        window.setTimeout(self.drawPrintSpeedMap, 0);
    }

    MakeItFilamentAnalyzerPrintSpeedViewModel.prototype = BaseViewModel.prototype;
    MakeItFilamentAnalyzerPrintSpeedViewModel.__makeitFaPrintSpeedV024 = true;
    registration[0] = MakeItFilamentAnalyzerPrintSpeedViewModel;
});
