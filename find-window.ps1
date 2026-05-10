# Find all windows containing "Cortex" in the title
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class WindowEnumerator {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc enumProc, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern int GetWindowTextLength(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
}
"@

$windows = New-Object System.Collections.ArrayList

$callback = {
    param($hWnd, $lParam)

    if ([WindowEnumerator]::IsWindowVisible($hWnd)) {
        $length = [WindowEnumerator]::GetWindowTextLength($hWnd)
        if ($length -gt 0) {
            $sb = New-Object System.Text.StringBuilder($length + 1)
            [WindowEnumerator]::GetWindowText($hWnd, $sb, $sb.Capacity) | Out-Null
            $title = $sb.ToString()

            if ($title -like "*Cortex*" -or $title -like "*Project*") {
                $windows.Add([PSCustomObject]@{
                    Handle = $hWnd
                    Title = $title
                }) | Out-Null
            }
        }
    }

    return $true
}

[WindowEnumerator]::EnumWindows($callback, [IntPtr]::Zero)

Write-Host "Windows containing 'Cortex' or 'Project':" -ForegroundColor Yellow
$windows | ForEach-Object {
    Write-Host "  - [$($_.Handle)] $($_.Title)" -ForegroundColor Cyan
}
