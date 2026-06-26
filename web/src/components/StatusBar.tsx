export function StatusBar() {
	return (
		<div className="bar">
			<button className="btn btn-off" onClick={() => fetch("/api/all/off")}>
				✕ WYŁĄCZ WSZYSTKO
			</button>
		</div>
	);
}
