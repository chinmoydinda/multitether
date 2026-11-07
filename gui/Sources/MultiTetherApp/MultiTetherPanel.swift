import AppKit
import SwiftUI
import MultiTetherIPC

struct MultiTetherPanel: View {
    @Bindable var model: MultiTetherModel

    private var needsScrollView: Bool {
        model.helperReady && !model.starting && !model.quitting && model.devices.count > 2
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            Divider().opacity(0.6)
            if needsScrollView {
                ScrollView {
                    panelContent
                }
                .frame(maxHeight: 440)
            } else {
                panelContent
            }
            Divider().opacity(0.6)
            footer
        }
        .frame(width: 412)
        .background(.regularMaterial)
        .tint(Color.primary)
        .preferredColorScheme(nil)
        .disabled(model.quitting)
        .confirmationDialog(L(.mtRemoveHelper), isPresented: $model.confirmRemoveHelper) {
            Button(L(.mtRemoveHelper), role: .destructive) {
                Task { await model.removeHelper() }
            }
        } message: { Text(L(.mtRemoveConfirm)) }
    }

    private var panelContent: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let error = model.serviceError {
                Self.statusBanner(
                    text: error,
                    systemImage: "exclamationmark.circle.fill",
                    tint: .red
                )
            }
            if model.quitting {
                loadingCard(text: L(.mtQuitting))
            } else if model.starting {
                loadingCard(
                    text: L(model.updatingHelper ? .mtInstallingHelper : .mtCheckingHelper)
                )
            } else if !model.helperReady {
                installHelperCard
            } else if model.devices.isEmpty {
                emptyState
            } else {
                ForEach(model.devices) { device in
                    PhoneCard(model: model, device: device)
                }
            }
        }
        .padding(14)
    }

    private var header: some View {
        HStack(spacing: 12) {
            ZStack {
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .fill(.quaternary.opacity(0.45))
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .strokeBorder(.primary.opacity(0.08), lineWidth: 1)
                Image(nsImage: MenuBarArtwork.image)
                    .renderingMode(.template)
                    .resizable()
                    .scaledToFit()
                    .frame(width: 20, height: 20)
                    .foregroundStyle(.primary)
            }
            .frame(width: 34, height: 34)

            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text("MultiTether")
                        .font(.system(size: 15, weight: .semibold))
                    if model.connectedCount > 0 {
                        HStack(spacing: 4) {
                            Circle()
                                .fill(Color.green)
                                .frame(width: 6, height: 6)
                            Text("\(model.connectedCount)")
                                .font(.system(size: 10, weight: .semibold, design: .rounded))
                                .foregroundStyle(.secondary)
                        }
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(.quaternary.opacity(0.45), in: Capsule())
                    }
                }
                Text(model.connectedCount > 0 ? L(.mtActiveCount, model.connectedCount) : L(.mtSubtitle))
                    .font(.system(size: 11.5))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            Spacer(minLength: 8)

            Button {
                Task { await model.refresh(forceDevices: true) }
            } label: {
                Image(systemName: "arrow.clockwise")
                    .font(.system(size: 12, weight: .medium))
                    .foregroundStyle(.secondary)
                    .frame(width: 28, height: 28)
                    .background(.quaternary.opacity(0.35), in: Circle())
                    .overlay(Circle().strokeBorder(.primary.opacity(0.06), lineWidth: 1))
            }
            .buttonStyle(.plain)
            .help(L(.mtRefresh))
            .disabled(model.starting || model.updatingHelper)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
    }

    private func loadingCard(text: String) -> some View {
        VStack(spacing: 10) {
            ProgressView()
                .controlSize(.regular)
            Text(text)
                .font(.system(size: 12.5, weight: .medium))
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 36)
        .padding(.horizontal, 16)
        .background(.quaternary.opacity(0.24), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(.primary.opacity(0.07), lineWidth: 1)
        )
    }

    private var installHelperCard: some View {
        VStack(spacing: 12) {
            ZStack {
                Circle()
                    .fill(.quaternary.opacity(0.4))
                    .frame(width: 46, height: 46)
                Image(systemName: "lock.shield")
                    .font(.system(size: 20, weight: .medium))
                    .foregroundStyle(.secondary)
            }

            VStack(spacing: 5) {
                Text(L(.mtInstallHelper))
                    .font(.system(size: 13.5, weight: .semibold))
                Text(L(.mtNetworkingUnavailable))
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Button {
                Task { await model.installHelper() }
            } label: {
                Text(L(.mtInstallHelper))
                    .font(.system(size: 12.5, weight: .medium))
                    .padding(.horizontal, 8)
            }
            .buttonStyle(.bordered)
            .controlSize(.regular)
            .disabled(model.starting || model.updatingHelper || model.anyOperation)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 22)
        .padding(.horizontal, 18)
        .background(.quaternary.opacity(0.26), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(.primary.opacity(0.08), lineWidth: 1)
        )
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            ZStack {
                Circle()
                    .fill(.quaternary.opacity(0.38))
                    .frame(width: 50, height: 50)
                Image(systemName: "iphone.and.arrow.forward")
                    .font(.system(size: 22, weight: .regular))
                    .foregroundStyle(.secondary)
            }

            VStack(spacing: 4) {
                Text(L(.mtEmptyTitle))
                    .font(.system(size: 13.5, weight: .semibold))
                Text(L(.mtEmptyBody))
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 28)
        .padding(.horizontal, 18)
        .background(.quaternary.opacity(0.22), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(.primary.opacity(0.06), lineWidth: 1)
        )
    }

    private var footer: some View {
        HStack(spacing: 10) {
            Button(L(.mtRemoveHelper)) { model.confirmRemoveHelper = true }
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .disabled(!model.helperReady || model.anyOperation || model.updatingHelper)

            Spacer(minLength: 6)

            if model.connectedCount > 0 {
                Self.speedPair(
                    download: model.totalRate.download,
                    upload: model.totalRate.upload
                )
                .padding(.horizontal, 9)
                .padding(.vertical, 3)
                .background(.quaternary.opacity(0.35), in: Capsule())
            }

            Button(L(.quit)) { NSApp.terminate(nil) }
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .help(L(.mtQuitHint))
        }
        .buttonStyle(.borderless)
        .font(.system(size: 11.5))
        .controlSize(.small)
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
    }

    fileprivate static func speedPair(download: Double, upload: Double) -> some View {
        HStack(spacing: 9) {
            HStack(spacing: 3) {
                Image(systemName: "arrow.down")
                Text(speed(download))
            }
            .lineLimit(1)
            .fixedSize(horizontal: true, vertical: false)

            HStack(spacing: 3) {
                Image(systemName: "arrow.up")
                Text(speed(upload))
            }
            .lineLimit(1)
            .fixedSize(horizontal: true, vertical: false)
        }
        .font(.system(size: 10.5, weight: .medium, design: .monospaced))
        .foregroundStyle(.secondary)
        .fixedSize(horizontal: true, vertical: false)
    }

    fileprivate static func statusBanner(text: String, systemImage: String, tint: Color) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: systemImage)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(tint)
                .padding(.top, 1)
            Text(text)
                .font(.system(size: 11.5))
                .foregroundStyle(.primary.opacity(0.9))
                .textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(tint.opacity(0.10), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .strokeBorder(tint.opacity(0.24), lineWidth: 1)
        )
    }

    static func speed(_ bits: Double) -> String {
        if bits >= 1_000_000 { return String(format: "%.1f Mbps", bits / 1_000_000) }
        if bits >= 1_000 { return String(format: "%.0f Kbps", bits / 1_000) }
        return "0 Kbps"
    }
}

private struct PhoneCard: View {
    @Bindable var model: MultiTetherModel
    let device: DeviceDescriptor

    private var connected: Bool { model.isConnected(device.id) }
    private var busy: Bool { model.operations[device.id] != nil }
    private var status: SessionStatus? { model.sessions[device.id]?.status }
    private var hasInterfaceStrip: Bool {
        connected || (status != nil && !(status?.systemInterface.isEmpty ?? true))
    }

    private var statusDotColor: Color {
        if connected { return .green }
        if busy { return .orange }
        if model.errorText(device.id) != nil { return .red }
        return .secondary.opacity(0.65)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .center, spacing: 11) {
                ZStack {
                    RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .fill(.quaternary.opacity(connected ? 0.55 : 0.35))
                    RoundedRectangle(cornerRadius: 9, style: .continuous)
                        .strokeBorder(.primary.opacity(0.07), lineWidth: 1)
                    Image(systemName: "iphone")
                        .font(.system(size: 18, weight: .regular))
                        .foregroundStyle(connected ? .primary : .secondary)
                }
                .frame(width: 36, height: 36)

                VStack(alignment: .leading, spacing: 3) {
                    Text(device.displayName)
                        .font(.system(size: 13, weight: .semibold))
                        .lineLimit(2)
                    HStack(spacing: 5) {
                        Circle()
                            .fill(statusDotColor)
                            .frame(width: 6, height: 6)
                        Text(model.statusText(device.id))
                            .font(.system(size: 11.5))
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                }

                Spacer(minLength: 6)

                Button {
                    Task {
                        if connected { await model.disconnect(device) }
                        else { await model.connect(device) }
                    }
                } label: {
                    if busy {
                        ProgressView()
                            .controlSize(.small)
                            .frame(width: 68)
                    } else {
                        Text(L(connected ? .disconnect : (model.errorText(device.id) == nil ? .connect : .mtRetry)))
                            .font(.system(size: 12, weight: .medium))
                            .frame(minWidth: 68)
                    }
                }
                .buttonStyle(.bordered)
                .controlSize(.regular)
                .disabled(busy || !model.helperReady || model.starting || model.updatingHelper)
            }

            if hasInterfaceStrip {
                HStack(spacing: 8) {
                    if let status, !status.systemInterface.isEmpty {
                        Text(status.systemInterface)
                            .font(.system(size: 10.5, weight: .semibold, design: .monospaced))
                            .lineLimit(1)
                            .fixedSize(horizontal: true, vertical: false)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .background(.quaternary.opacity(0.45), in: RoundedRectangle(cornerRadius: 5, style: .continuous))
                        if let network = model.networks[device.id], network.hasAddress {
                            Text(network.address)
                                .font(.system(size: 11, design: .monospaced))
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                                .fixedSize(horizontal: true, vertical: false)
                        }
                    }

                    Spacer(minLength: 4)

                    if connected {
                        MultiTetherPanel.speedPair(
                            download: model.rates[device.id]?.download ?? 0,
                            upload: model.rates[device.id]?.upload ?? 0
                        )
                    } else if let status, !status.systemInterface.isEmpty, !busy {
                        Button(L(.disconnect)) {
                            Task { await model.disconnect(device) }
                        }
                        .buttonStyle(.borderless)
                        .controlSize(.small)
                        .font(.system(size: 11, weight: .medium))
                        .foregroundStyle(.secondary)
                    }
                }
                .textSelection(.enabled)
                .padding(.horizontal, 9)
                .padding(.vertical, 6)
                .background(.quaternary.opacity(0.25), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            }

            if let error = model.errorText(device.id) {
                MultiTetherPanel.statusBanner(
                    text: error,
                    systemImage: "exclamationmark.octagon.fill",
                    tint: .red
                )
            }

            if let warning = model.warningText(device.id) {
                MultiTetherPanel.statusBanner(
                    text: warning,
                    systemImage: "exclamationmark.triangle.fill",
                    tint: .orange
                )
            }
        }
        .padding(12)
        .background(
            .quaternary.opacity(connected ? 0.42 : 0.28),
            in: RoundedRectangle(cornerRadius: 12, style: .continuous)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(.primary.opacity(connected ? 0.12 : 0.07), lineWidth: 1)
        )
    }
}
