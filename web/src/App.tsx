import { useEffect, useState } from "react";
import {
	getInfo,
	getSchedule,
	getZones,
	saveSchedule,
	zoneCommand,
} from "./api/esp32";
import { Header } from "./components/Header";
import { OtaUpload } from "./components/OtaUpload";
import { ScheduleTable } from "./components/ScheduleTable";
import { StatusBar } from "./components/StatusBar";
import { ZoneGrid } from "./components/ZoneGrid";
import type { Schedule, SystemInfo, Zone } from "./types";

function App() {
	const [zones, setZones] = useState<Zone[]>([]);
	const [schedule, setSchedule] = useState<Schedule[]>([]);
	const [info, setInfo] = useState<SystemInfo | null>(null);

	useEffect(() => {
		loadData();
		const interval = setInterval(loadData, 5000);
		return () => clearInterval(interval);
	}, []);

	async function loadData() {
		try {
			const [zonesRes, scheduleRes, infoRes] = await Promise.all([
				getZones(),
				getSchedule(),
				getInfo(),
			]);
			setZones(zonesRes.data);
			setSchedule(scheduleRes.data);
			setInfo(infoRes.data);
		} catch (err) {
			console.error("Failed to load data:", err);
		}
	}

	async function handleCommand(id: number, action: string) {
		await zoneCommand(id, action);
		loadData();
	}

	async function handleRun(id: number) {
		await zoneCommand(id, "run", 300);
		loadData();
	}

	async function handleSave(newSchedule: Schedule[]) {
		await saveSchedule(newSchedule);
		alert("✅ Harmonogram zapisany");
		loadData();
	}

	return (
		<div className="app">
			<Header info={info} />
			<ZoneGrid zones={zones} onCommand={handleCommand} onRun={handleRun} />
			<StatusBar />
			<ScheduleTable schedule={schedule} onSave={handleSave} />
			<OtaUpload />
		</div>
	);
}

export default App;
