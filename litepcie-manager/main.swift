// Taken from Karabiner (Unlicensed license), who have a good shim app solution for driverhit here
// https://github.com/pqrs-org/Karabiner-DriverKit-VirtualHIDDevice/blob/main/src/Manager/Sources/Manager/ExtensionManager.swift

import Foundation

RunLoop.main.perform {
  for argument in CommandLine.arguments {
    if argument == "activate" {
      ExtensionManager.shared.activate(forceReplace: false)
      return
    } else if argument == "forceActivate" {
      ExtensionManager.shared.activate(forceReplace: true)
      return
    } else if argument == "deactivate" {
      ExtensionManager.shared.deactivate()
      return
    }
  }

  print("Usage:")
  print("    litepcie-manager activate|forceActivate|deactivate")
  exit(0)
}

RunLoop.main.run()
