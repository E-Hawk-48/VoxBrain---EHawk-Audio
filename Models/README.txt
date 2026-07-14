VocalForge neural models
========================

crepe-tiny.onnx — CREPE pitch-estimation model (tiny variant), ONNX format.
Source: ailia-models export of the original CREPE weights (Kim et al., 2018,
"CREPE: A Convolutional Representation for Pitch Estimation", ICML — MIT licensed).

The build copies this folder next to every built binary as "VocalForgeModels".
The plugin also looks in:  %APPDATA%\VocalForge\Models\

If this file is missing the plugin still works — analysis falls back to the
built-in DSP pitch tracker (YIN).
