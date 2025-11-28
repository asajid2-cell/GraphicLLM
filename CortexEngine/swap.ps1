# 1. Setup Paths
$binDir = "Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\bin"
$modelsDir = "$binDir\models"
$oldModel = "$modelsDir\Llama-3.2-3B-Instruct-Q4_K_M.gguf"
$backupModel = "$modelsDir\Llama-3.2-3B.bak"

# 2. Define Llama 3.1 8B URL (Stable, High Compatibility)
$newModelUrl = "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"

# 3. Switch Models
if (Test-Path $modelsDir) {
    cd $modelsDir
    
    # Backup the broken 3.2 model if we haven't already
    if ((Test-Path $oldModel) -and (-not (Test-Path $backupModel))) {
        Write-Host "Backing up Llama 3.2..." -ForegroundColor Yellow
        Rename-Item -Path $oldModel -NewName "Llama-3.2-3B.bak"
    }

    # Download Llama 3.1 8B
    Write-Host "Downloading Llama 3.1 8B (The Stable Brain)..." -ForegroundColor Cyan
    Write-Host "Size: ~4.9 GB (Fits in your RTX 3070 Ti)" -ForegroundColor Gray
    
    $webClient = New-Object System.Net.WebClient
    $webClient.DownloadFile($newModelUrl, "temp_model.gguf")
    
    # Rename it to what the engine expects (The "Mask")
    Move-Item "temp_model.gguf" $oldModel -Force
    
    Write-Host "Success! Brain swapped." -ForegroundColor Green
} else {
    Write-Host "Error: Could not find models directory at $modelsDir" -ForegroundColor Red
}
```

### How to Run the Engine (After Swapping)
Once the download finishes, launch the engine from the `bin` folder like before:

```powershell
cd "Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\bin"
.\CortexEngine.exe