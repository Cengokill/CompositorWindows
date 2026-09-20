// Reference instrumentation for Compositor a19db9011282399785dc18efcfded904627bdcc2.
// Copy into an isolated worktree's CompositorTests using capture_mac.py.
// This file has not been compiled or run on macOS in the Windows audit.
import AppKit
import CoreGraphics
import Foundation
import Testing
@testable import Compositor

@MainActor
struct MacReferenceExporter {
    private let baseline = "a19db9011282399785dc18efcfded904627bdcc2"
    private let rootPathBase64 = "__OUTPUT_PATH_BASE64__"
    private var root: URL {
        URL(fileURLWithPath: String(data: Data(base64Encoded: rootPathBase64)!, encoding: .utf8)!, isDirectory: true)
    }
    private func uuid(_ number: Int) -> UUID {
        UUID(uuidString: String(format: "00000000-0000-4000-8000-%012d", number))!
    }
    private func encode<T: Encodable>(_ value: T, to path: URL) throws {
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(value).write(to: path, options: .atomic)
    }
    private func synthetic(width: Int = 17, height: Int = 13, mask: Bool = false) throws -> ImportedImage {
        let channels = mask ? 1 : 4
        var bytes = [UInt8](repeating: 0, count: width * height * channels)
        for y in 0..<height { for x in 0..<width {
            let alpha = (x * 31 + y * 17) % 256
            let i = (y * width + x) * channels
            if mask { bytes[i] = UInt8(alpha) }
            else {
                bytes[i] = UInt8(((x * 19) % 256) * alpha / 255)
                bytes[i+1] = UInt8(((y * 29) % 256) * alpha / 255)
                bytes[i+2] = UInt8(((x * 11 + y * 7) % 256) * alpha / 255)
                bytes[i+3] = UInt8(alpha)
            }
        }}
        let space = mask ? CGColorSpaceCreateDeviceGray() : CGColorSpace(name: CGColorSpace.sRGB)!
        let bitmap = mask ? CGImageAlphaInfo.none.rawValue : CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue
        let image = try #require(CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: channels * 8,
            bytesPerRow: width * channels, space: space, bitmapInfo: CGBitmapInfo(rawValue: bitmap),
            provider: CGDataProvider(data: Data(bytes) as CFData)!, decode: nil, shouldInterpolate: false, intent: .defaultIntent))
        return ImportedImage(image: image, thumbnail: image, name: mask ? "Synthetic coverage" : "Synthetic RGBA")
    }
    private func rgba(_ image: CGImage) throws -> Data {
        let context = try BrushRaster.context(width: image.width, height: image.height, mask: false)
        BrushRaster.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height), mask: false, context: context)
        let bytes = try #require(context.data).assumingMemoryBound(to: UInt8.self)
        var result = Data()
        for y in 0..<image.height {
            result.append(bytes.advanced(by: y * context.bytesPerRow), count: image.width * 4)
        }
        return result
    }
    private struct FixtureRecord: Codable {
        let id: String
        let version: Int
        let package: String
        let flattened: String
        let raw: String
        let width: Int
        let height: Int
        let resolution: Double
        let operations: [String]
        let source: [String]
        let readerValidated: Bool
    }
    private func save(_ snapshot: ProjectSnapshot, id: String, operations: [String]) async throws -> FixtureRecord {
        let url = root.appendingPathComponent(id + ".comp", isDirectory: true)
        try await ProjectStore.shared.save(snapshot, to: url)
        let loaded = try await ProjectStore.shared.load(from: url)
        #expect(loaded.manifest.version == snapshot.manifest.version)
        #expect(loaded.manifest.layers.map(\.id) == snapshot.manifest.layers.map(\.id))
        #expect(loaded.manifest.layers.map(\.transform) == snapshot.manifest.layers.map(\.transform))
        let raster = try await ImageExporter.shared.render(loaded)
        let png = try await ImageExporter.shared.pngData(loaded)
        try png.write(to: root.appendingPathComponent(id + ".png"), options: .atomic)
        try rgba(raster.image).write(to: root.appendingPathComponent(id + ".rgba"), options: .atomic)
        let installed = EditorSession(); installed.installProject(loaded, from: url)
        #expect(installed.isModified == false && installed.canUndo == false)
        #expect(installed.document?.selection == nil)
        return FixtureRecord(id: id, version: loaded.manifest.version, package: id + ".comp",
            flattened: id + ".png", raw: id + ".rgba", width: raster.image.width, height: raster.image.height,
            resolution: raster.resolution, operations: operations,
            source: ["Compositor/IO/ProjectStore.swift", "Compositor/IO/ImageExporter.swift", "Compositor/Document/EditorSession+Projects.swift"], readerValidated: true)
    }
    @Test func exportAll() async throws {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        // Codable probes record Foundation geometry and enum-keyed Dictionary wire shapes.
        var transform = LayerTransform(origin: CGPoint(x: -2.25, y: 3.5), size: CGSize(width: 17, height: 13))
        transform.rotation = 27; transform.flipX = true; transform.sampling = .nearest
        try encode(transform, to: root.appendingPathComponent("codable-transform.json"))
        var hsv = HueSaturationSettings(hue: 37, saturation: -12, lightness: 5)
        hsv.range = .reds; hsv.invertRange = true; hsv.adjustments[.reds] = RangeAdjustment(hue: -10, saturation: 22, lightness: 3)
        try encode(hsv, to: root.appendingPathComponent("codable-hsv.json"))
        let asset = try synthetic(); let mask = try synthetic(mask: true)
        let baseID = uuid(1), topID = uuid(2), folderID = uuid(3)
        let ordinary = LayerTransform(origin: .zero, size: CGSize(width: 17, height: 13))
        var records: [FixtureRecord] = []
        // Each version is constructed using only that version's features and then read by the pinned reader.
        // These are generated legacy records; they are not asserted to have been produced by historic app binaries.
        for version in 1...7 {
            var base = ProjectLayerRecord(id: baseID, name: "Synthetic base", isVisible: true, transform: transform, imageFile: "\(baseID.uuidString).png")
            var layers = [base]
            var images = [baseID: asset]; var masks: [UUID: ImportedImage] = [:]
            if version >= 2 {
                base.parentID = folderID; layers = [ProjectLayerRecord(id: folderID, name: "Folder", isVisible: true, transform: ordinary, imageFile: nil, isGroup: true), base]
            }
            if version >= 3 { layers[layers.count-1].opacity = 0.625; layers[layers.count-1].blendMode = .multiply }
            if version >= 4 { layers[layers.count-1].maskFile = "\(baseID.uuidString).mask.png"; layers[layers.count-1].maskEnabled = true; masks[baseID] = mask }
            if version >= 5 {
                layers.append(ProjectLayerRecord(id: topID, name: "Clipped top", isVisible: true, transform: ordinary,
                    imageFile: "\(topID.uuidString).png", parentID: folderID, maskSourceID: baseID))
                images[topID] = asset
            }
            if version >= 6 { layers[0].maskFile = "\(folderID.uuidString).mask.png"; layers[0].maskEnabled = true; masks[folderID] = mask }
            if version >= 7 {
                var adjustment = LayerAdjustment(kind: .hsv); adjustment.hsvSettings = hsv
                layers.append(ProjectLayerRecord(id: uuid(4), name: "HSV", isVisible: true, transform: ordinary, imageFile: nil, adjustment: adjustment))
                layers[1].maskPlacement = transform; layers[1].maskLinked = false
                layers[1].shape = LayerShapeStyle(kind: .rectangle, red: 0.2, green: 0.3, blue: 0.8, cornerRadius: 3)
            }
            var manifest = ProjectManifest(documentID: uuid(100+version), width: 19, height: 17, activeLayerID: baseID, layers: layers)
            manifest.version = version; manifest.resolution = version == 1 ? nil : 144
            records.append(try await save(ProjectSnapshot(manifest: manifest, images: images, masks: masks), id: "schema-v\(version)", operations: ["construct version-specific record", "save", "load with pinned reader", "render", "install and assert clean state"]))
        }
        for (index, mode) in LayerBlendMode.allCases.enumerated() {
            let bottom = ProjectLayerRecord(id: baseID, name: "Bottom", isVisible: true, transform: ordinary, imageFile: "\(baseID.uuidString).png")
            let top = ProjectLayerRecord(id: topID, name: mode.rawValue, isVisible: true, transform: transform,
                imageFile: "\(topID.uuidString).png", opacity: 0.7, blendMode: mode, maskFile: "\(topID.uuidString).mask.png", maskEnabled: true)
            let manifest = ProjectManifest(documentID: uuid(200+index), width: 19, height: 17, activeLayerID: topID, layers: [bottom, top])
            records.append(try await save(ProjectSnapshot(manifest: manifest, images: [baseID: asset, topID: asset], masks: [topID: mask]), id: "blend-\(index)-\(mode.rawValue.replacingOccurrences(of: " ", with: "-"))", operations: ["translucent RGBA", "rotated/flipped top", "grayscale mask", "opacity 0.7", "blend \(mode.rawValue)"]))
        }
        for (index, kind) in AdjustmentKind.allCases.enumerated() {
            var adjustment = LayerAdjustment(kind: kind)
            adjustment.hsvSettings = hsv
            adjustment.levels.ranges[0].gamma = 1.3
            adjustment.curves.channels[0].insert(CurvePoint(x: 120, y: 156), at: 1)
            adjustment.exposure = ExposureSettings(exposure: 0.5, offset: 0.01, gamma: 1.1)
            adjustment.gradientMap = GradientMapSettings(shadows: AdjustmentColor(red: 0.1, green: 0.2, blue: 0.3), highlights: AdjustmentColor(red: 0.9, green: 0.8, blue: 0.4), reversed: true)
            adjustment.grain = GrainSettings(amount: 25, size: 1.5, roughness: 50, seed: 0xC04F05)
            try encode(adjustment, to: root.appendingPathComponent("codable-adjustment-\(index).json"))
            let base = ProjectLayerRecord(id: baseID, name: "Base", isVisible: true, transform: ordinary, imageFile: "\(baseID.uuidString).png")
            let layer = ProjectLayerRecord(id: topID, name: kind.rawValue, isVisible: true, transform: ordinary, imageFile: nil, adjustment: adjustment)
            let manifest = ProjectManifest(documentID: uuid(300+index), width: 19, height: 17, activeLayerID: topID, layers: [base, layer])
            records.append(try await save(ProjectSnapshot(manifest: manifest, images: [baseID: asset]), id: "adjustment-\(index)", operations: ["apply \(kind.rawValue)", "fixed grain seed 0xC04F05", "persist settings"]))
        }
        try encode(records, to: root.appendingPathComponent("fixtures.json"))
        let environment: [String: String] = ["baseline_sha": baseline, "os": ProcessInfo.processInfo.operatingSystemVersionString,
            "screen_scale": String(Double(NSScreen.main?.backingScaleFactor ?? 0)),
            "screen_color_space": NSScreen.main?.colorSpace?.localizedName ?? "unavailable",
            "raw_format": "top-left, tightly packed RGBA8 premultiplied alpha, sRGB, width*4 stride",
            "seed": "0xC04F05", "status": "exporter_completed_inspect_xcresult_for_assertion_result"]
        try encode(environment, to: root.appendingPathComponent("environment.json"))
    }
}
