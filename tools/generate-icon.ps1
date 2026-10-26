$ErrorActionPreference='Stop'
$taskSizes=@(16,32,48,64)
$taskImages=[System.Collections.Generic.List[byte[]]]::new()
$taskPolyX=@(35,17,29,26,49,35)
$taskPolyY=@(9,35,35,56,27,27)
# Иконка строится из той же геометрии, что и SVG. Сглаживание — четыре отсчёта по каждой оси.
foreach($taskSize in $taskSizes){
  $taskStream=[System.IO.MemoryStream]::new()
  $taskWriter=[System.IO.BinaryWriter]::new($taskStream)
  $taskWriter.Write([uint32]40);$taskWriter.Write([int32]$taskSize);$taskWriter.Write([int32]($taskSize*2))
  $taskWriter.Write([uint16]1);$taskWriter.Write([uint16]32);$taskWriter.Write([uint32]0)
  $taskMaskStride=[int]([math]::Ceiling($taskSize/32)*4)
  $taskWriter.Write([uint32]($taskSize*$taskSize*4+$taskMaskStride*$taskSize))
  $taskWriter.Write([int32]0);$taskWriter.Write([int32]0);$taskWriter.Write([uint32]0);$taskWriter.Write([uint32]0)
  for($taskY=$taskSize-1;$taskY -ge 0;$taskY--){
    for($taskX=0;$taskX -lt $taskSize;$taskX++){
      $taskR=0.0;$taskG=0.0;$taskB=0.0;$taskA=0.0
      for($taskSy=0;$taskSy -lt 4;$taskSy++){for($taskSx=0;$taskSx -lt 4;$taskSx++){
        $taskPx=($taskX+($taskSx+0.5)/4)*64/$taskSize;$taskPy=($taskY+($taskSy+0.5)/4)*64/$taskSize
        $taskQx=[math]::Max([math]::Abs($taskPx-32)-17,0);$taskQy=[math]::Max([math]::Abs($taskPy-32)-17,0)
        if($taskQx*$taskQx+$taskQy*$taskQy -gt 169){continue}
        $taskInside=$false
        for($taskI=0;$taskI -lt 6;$taskI++){
          $taskJ=($taskI+5)%6
          if((($taskPolyY[$taskI] -gt $taskPy) -ne ($taskPolyY[$taskJ] -gt $taskPy)) -and ($taskPx -lt ($taskPolyX[$taskJ]-$taskPolyX[$taskI])*($taskPy-$taskPolyY[$taskI])/($taskPolyY[$taskJ]-$taskPolyY[$taskI])+$taskPolyX[$taskI])){$taskInside=-not $taskInside}
        }
        if($taskInside){$taskR+=255;$taskG+=255;$taskB+=255}else{
          $taskT=($taskPx+$taskPy)/128
          $taskR+=169+(115-169)*$taskT;$taskG+=131+(80-131)*$taskT;$taskB+=251+(222-251)*$taskT
        }
        $taskA+=255
      }}
      $taskWriter.Write([byte]($taskB/16));$taskWriter.Write([byte]($taskG/16));$taskWriter.Write([byte]($taskR/16));$taskWriter.Write([byte]($taskA/16))
    }
  }
  $taskWriter.Write([byte[]]::new($taskMaskStride*$taskSize))
  $taskImages.Add($taskStream.ToArray());$taskWriter.Dispose();$taskStream.Dispose()
}
$taskIcoStream=[System.IO.MemoryStream]::new();$taskIcoWriter=[System.IO.BinaryWriter]::new($taskIcoStream)
$taskIcoWriter.Write([uint16]0);$taskIcoWriter.Write([uint16]1);$taskIcoWriter.Write([uint16]$taskSizes.Count)
$taskOffset=6+16*$taskSizes.Count
for($taskI=0;$taskI -lt $taskSizes.Count;$taskI++){
  $taskIcoWriter.Write([byte]$taskSizes[$taskI]);$taskIcoWriter.Write([byte]$taskSizes[$taskI]);$taskIcoWriter.Write([byte]0);$taskIcoWriter.Write([byte]0)
  $taskIcoWriter.Write([uint16]1);$taskIcoWriter.Write([uint16]32);$taskIcoWriter.Write([uint32]$taskImages[$taskI].Length);$taskIcoWriter.Write([uint32]$taskOffset)
  $taskOffset+=$taskImages[$taskI].Length
}
foreach($taskBytes in $taskImages){$taskIcoWriter.Write($taskBytes)}
$taskIconPath=Join-Path (Split-Path $PSScriptRoot -Parent) 'assets/Sparkshot.ico'
[System.IO.File]::WriteAllBytes($taskIconPath,$taskIcoStream.ToArray())
$taskIcoWriter.Dispose();$taskIcoStream.Dispose()
