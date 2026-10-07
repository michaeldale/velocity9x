param([string]$Source, [string]$Destination, [int]$X = 80, [int]$Y = 80, [int]$W = 640, [int]$H = 480)
# A window's area of an agent screenshot (BMP) as a PNG.
Add-Type -AssemblyName System.Drawing
$image = [Drawing.Image]::FromFile($Source)
$crop = New-Object Drawing.Bitmap $W, $H
$g = [Drawing.Graphics]::FromImage($crop)
$g.DrawImage($image, (New-Object Drawing.Rectangle 0, 0, $W, $H), (New-Object Drawing.Rectangle $X, $Y, $W, $H), [Drawing.GraphicsUnit]::Pixel)
$crop.Save($Destination, [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$crop.Dispose()
$image.Dispose()
