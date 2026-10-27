import CoreBluetooth
import Foundation

struct Detection: Identifiable, Codable {
    var id = UUID()
    var type: String
    var mac: String
    var rssi: Int
    var confidence: String
    var ts: Int
    var receivedAt = Date()
}

let kSvcUUID    = CBUUID(string: "4fafc201-1fb5-459e-8fcc-c5c9c331914b")
let kDetUUID    = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26a8")
let kStreamUUID = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26aa")

class BLEManager: NSObject, ObservableObject, CBCentralManagerDelegate,
                  CBPeripheralDelegate {
    @Published var detections: [Detection] = []
    @Published var streamLines: [String] = []
    @Published var connected = false
    @Published var scanning = false

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        if c.state == .poweredOn { startScan() }
    }

    func startScan() {
        detections.removeAll(); streamLines.removeAll()
        scanning = true
        central.scanForPeripherals(withServices: [kSvcUUID],
                                   options: nil)
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover p: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        peripheral = p
        p.delegate = self
        central.stopScan()
        scanning = false
        central.connect(p, options: nil)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        connected = true
        p.discoverServices([kSvcUUID])
    }

    func centralManager(_ c: CBCentralManager,
                        didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        connected = false
        peripheral = nil
        startScan()   // auto-reconnect
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        p.services?.forEach { p.discoverCharacteristics(nil, for: $0) }
    }

    func peripheral(_ p: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        service.characteristics?.forEach { ch in
            p.setNotifyValue(true, for: ch)
        }
    }

    func peripheral(_ p: CBPeripheral,
                    didUpdateValueFor ch: CBCharacteristic,
                    error: Error?) {
        guard let data = ch.value,
              let text = String(data: data, encoding: .utf8),
              let json = text.data(using: .utf8) else { return }
        if ch.uuid == kDetUUID {
            if let d = try? JSONDecoder().decode(Detection.self,
                                                 from: json) {
                DispatchQueue.main.async {
                    self.detections.insert(d, at: 0)
                    if self.detections.count > 200 {
                        self.detections.removeLast()
                    }
                }
            }
        } else if ch.uuid == kStreamUUID {
            if let obj = try? JSONSerialization.jsonObject(
                   with: json) as? [String: Any],
               let line = obj["line"] as? String {
                DispatchQueue.main.async {
                    self.streamLines.insert(line, at: 0)
                    if self.streamLines.count > 100 {
                        self.streamLines.removeLast()
                    }
                }
            }
        }
    }
}
