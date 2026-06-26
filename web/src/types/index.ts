export interface Zone {
	id: number;
	name: string;
	active: boolean;
	remaining: number;
	enabled: boolean;
}

export interface Schedule {
	id: number;
	name: string;
	enabled: boolean;
	days: number;
	startHour: number;
	startMinute: number;
	duration: number;
}

export interface SystemInfo {
	version: string;
	time: string;
	synced: boolean;
	uptime: number;
	heap: number;
	mqtt: boolean;
	rain: boolean;
}
