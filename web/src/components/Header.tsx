import type { SystemInfo } from "../types";

interface HeaderProps {
	info: SystemInfo | null;
}

export function Header({ info }: HeaderProps) {
	return (
		<header className="header">
			<h1>💧 Nawodnienie ogrodu</h1>
			{info && (
				<div className="status-bar">
					<span>⏰ {info.time}</span>
					<span>{info.rain ? "🌧 DESZCZ" : "🌤 sucho"}</span>
					<span>📡 MQTT: {info.mqtt ? "połączony" : "brak"}</span>
				</div>
			)}
		</header>
	);
}
