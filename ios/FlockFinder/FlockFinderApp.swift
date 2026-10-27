import SwiftUI

@main
struct FlockFinderApp: App {
    @StateObject private var ble = BLEManager()
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ble)
        }
    }
}
