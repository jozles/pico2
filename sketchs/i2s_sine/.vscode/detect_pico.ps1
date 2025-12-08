# Détection automatique du port COM du Raspberry Pi Pico
$device = Get-WmiObject Win32_PnPEntity | Where-Object { $_.Name -like "*USB Serial Device*" }

if ($device -ne $null) {
    # Exemple : "USB Serial Device (COM4)" → on extrait COM4
    $comPort = ($device.Name -split "\(")[1].TrimEnd(")")
    Write-Output "Pico détecté sur $comPort"

    # Lancer le Serial Monitor de VS Code sur ce port
    code --command "serialmonitor.connect $comPort"
    code --command "serialmonitor.start"
} else {
    Write-Output "Aucun Pico détecté."
}
