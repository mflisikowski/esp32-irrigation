import { useRef, useState } from "react";

export function OtaUpload() {
	const [uploading, setUploading] = useState(false);
	const [progress, setProgress] = useState(0);
	const [status, setStatus] = useState<string | null>(null);
	const fileInput = useRef<HTMLInputElement>(null);

	async function handleUpload() {
		const file = fileInput.current?.files?.[0];
		if (!file) return;

		setUploading(true);
		setStatus("Uploading...");
		setProgress(0);

		try {
			const formData = new FormData();
			formData.append("file", file);

			const xhr = new XMLHttpRequest();
			xhr.open("POST", "/api/ota");

			xhr.upload.onprogress = (e) => {
				if (e.lengthComputable) {
					setProgress(Math.round((e.loaded / e.total) * 100));
				}
			};

			xhr.onload = () => {
				if (xhr.status === 200) {
					setStatus("✅ Update complete! Rebooting...");
				} else {
					setStatus("❌ Upload failed");
					setUploading(false);
				}
			};

			xhr.onerror = () => {
				setStatus("❌ Network error");
				setUploading(false);
			};

			xhr.send(formData);
		} catch (err) {
			setStatus("❌ Error: " + String(err));
			setUploading(false);
		}
	}

	return (
		<div className="schedule">
			<h2>🔄 Aktualizacja firmware</h2>
			<div
				style={{
					display: "flex",
					gap: "12px",
					alignItems: "center",
					flexWrap: "wrap",
				}}
			>
				<input
					type="file"
					ref={fileInput}
					accept=".bin"
					disabled={uploading}
					style={{ flex: 1 }}
				/>
				<button
					className="btn-save"
					onClick={handleUpload}
					disabled={uploading}
				>
					{uploading ? `Wgrywanie... ${progress}%` : "🔄 WGRAJ FIRMWARE"}
				</button>
			</div>
			{uploading && (
				<div style={{ marginTop: "12px" }}>
					<div
						style={{
							height: "8px",
							background: "#333",
							borderRadius: "4px",
							overflow: "hidden",
						}}
					>
						<div
							style={{
								height: "100%",
								width: `${progress}%`,
								background: "#e94560",
								transition: "width 0.3s",
							}}
						/>
					</div>
				</div>
			)}
			{status && (
				<div style={{ marginTop: "12px", color: "#aaa", fontSize: "0.85rem" }}>
					{status}
				</div>
			)}
		</div>
	);
}
