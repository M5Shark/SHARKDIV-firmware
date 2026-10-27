import SwiftUI

struct ContentView: View {
    @EnvironmentObject var ble: BLEManager

    var body: some View {
        NavigationView {
            List {
                Section {
                    HStack {
                        Circle()
                            .fill(ble.connected ? Color.green : Color.orange)
                            .frame(width: 10, height: 10)
                        Text(ble.connected ? "SHARKDIV connected"
                                           : (ble.scanning ? "Scanning…" : "Disconnected"))
                        Spacer()
                        Text("\(ble.detections.count) detections")
                            .font(.caption).foregroundColor(.secondary)
                    }
                }
                Section("Detections") {
                    if ble.detections.isEmpty {
                        Text("None yet — walk near a camera")
                            .foregroundColor(.secondary)
                    }
                    ForEach(ble.detections) { d in
                        VStack(alignment: .leading, spacing: 2) {
                            HStack {
                                Text(d.mac).font(.system(.body, design: .monospaced))
                                Spacer()
                                Text(d.confidence)
                                    .font(.caption.weight(.bold))
                                    .foregroundColor(color(d.confidence))
                            }
                            HStack {
                                Text("\(d.rssi) dBm").font(.caption)
                                Text(d.type.uppercased()).font(.caption)
                                    .foregroundColor(.secondary)
                                Spacer()
                                Text(d.receivedAt, style: .time)
                                    .font(.caption).foregroundColor(.secondary)
                            }
                        }
                    }
                }
                Section("Raw stream") {
                    ForEach(ble.streamLines.prefix(20), id: \.self) { line in
                        Text(line).font(.system(size: 11, design: .monospaced))
                    }
                }
            }
            .navigationTitle("FlockFinder")
        }
    }

    func color(_ c: String) -> Color {
        switch c {
        case "CONFIRMED": return .green
        case "LIKELY": return .orange
        default: return .gray
        }
    }
}
