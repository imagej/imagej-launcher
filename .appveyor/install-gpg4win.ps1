# install-gpg4win.ps1 - A Powershell script to install GPG4Win.
$filename = "gpg4win-vanilla-2.3.4.exe"

# === Do not change below this line ===
"`n== Installing GPG4Win =="
$url = "https://files.gpg4win.org/" + $filename
$setupFilename = "C:\" + $filename
$installFolder = $Env:GNUPG_HOME

# Download installer from files.gpg4win.org
If (!(Test-Path $setupFilename)) {
    "Downloading installer"
    Invoke-WebRequest -Uri $url -OutFile $setupFilename
} Else {
    "Installer found: not attempting to download"
}

# Install GPG4Win
"Starting installer"
Invoke-Expression ("" + $setupFilename + " /S /D=" + $installFolder)
$pollCounter = 0;
# Wait for gpg2.exe to exists
while (!(Test-Path ("" + $installFolder + "\gpg2.exe")) -and ($pollCounter -le 10)) {
    Start-Sleep 5
    $pollCounter++
    Write-Output ("Waiting for installer to finish (#" + $pollCounter + ")")
}
"Installation done"
exit 0
