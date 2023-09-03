import Foundation
import Capacitor

@objc(Scan)
public class Scan: CAPPlugin {
    
    private var scan: ScanBridge?
    private var isInit = false
    
    private let template = "{\"output\": \"%@\"}"
    @objc public func sendOutput(_ output: String) {
        bridge?.triggerWindowJSEvent(eventName: "scan", data: String(format: template, output))
    }

    @objc override public func load() {
        var onPauseWorkItem: DispatchWorkItem?

        NotificationCenter.default.addObserver(forName: UIApplication.willResignActiveNotification, object: nil, queue: OperationQueue.main) { [weak self] (_) in
            if (self!.isInit) {
                onPauseWorkItem = DispatchWorkItem {
                    self?.scan?.cmd("stop")
                }
                DispatchQueue.main.asyncAfter(deadline: .now() + 60 * 3, execute: onPauseWorkItem!)
            }
        }
        
        NotificationCenter.default.addObserver(forName: UIApplication.didBecomeActiveNotification, object: nil, queue: OperationQueue.main) { [weak self] (_) in
            if (self!.isInit) {
                onPauseWorkItem?.cancel()
            }
        }

        NotificationCenter.default.addObserver(forName: UIApplication.willTerminateNotification, object: nil, queue: OperationQueue.main) { [weak self] (_) in
            if (self!.isInit) {
                self?.scan?.cmd("stop")
                self?.scan?.exit()
                self?.isInit = false
            }
        }
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }
    
    @objc func getCPUArch(_ call: CAPPluginCall) {
        call.resolve([
            "value": ScanBridge.getCPUType() ?? "unknown"
        ])
    }

    @objc func getMaxMemory(_ call: CAPPluginCall) {
        // allow max 1/16th of total mem
        let maxMemInMb = (ProcessInfo().physicalMemory / 16) / (1024 * 1024)
        call.resolve([
            "value": maxMemInMb
        ])
    }

    @objc func start(_ call: CAPPluginCall) {
        if (!isInit) {
            scan = ScanBridge(plugin: self)
            scan?.start()
            isInit = true
        }
        call.resolve()
    }

    @objc func cmd(_ call: CAPPluginCall) {
        if (isInit) {
            guard let cmd = call.options["cmd"] as? String else {
                call.reject("Must provide a cmd")
                return
            }
            scan?.cmd(cmd)
            call.resolve()
        } else {
            call.reject("You must call start before anything else")
        }
    }
    
    @objc func exit(_ call: CAPPluginCall) {
        if (isInit) {
            scan?.cmd("quit")
            scan?.exit()
            isInit = false
        }
        call.resolve()
    }
}
