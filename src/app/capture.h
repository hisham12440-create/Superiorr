#pragma once
#include <unknwn.h>
#include <windows.h>
#include <d3d11.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace superior {

// Windows.Graphics.Capture of one window, GPU only (no CPU readback).
// Capture-only: nothing is injected into or hooked from the game process.
class Capture {
public:
    Capture();
    ~Capture();

    static std::optional<bool> Supported();
    bool Start(HWND target, ID3D11Device* dev, HANDLE frameEvent, std::string& error);
    void Stop();
    bool Active() const;
    bool Closed() const;  // the captured window was closed

    // Delivers the newest available frame (older ones are dropped). Returns true if one was delivered.
    bool Poll(const std::function<void(ID3D11Texture2D*, UINT, UINT)>& onFrame);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace superior
