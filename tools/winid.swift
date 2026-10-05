import CoreGraphics
let opts = CGWindowListOption(arrayLiteral: .optionAll)
if let list = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as? [[String: Any]] {
  for w in list {
    let owner = w[kCGWindowOwnerName as String] as? String ?? ""
    if owner.contains("PPSSPP") {
      let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
      if let h = b["Height"] as? Double, h > 100 { print(w[kCGWindowNumber as String]!); break }
    }
  }
}
