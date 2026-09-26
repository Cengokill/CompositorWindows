param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$Version
)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$package=[IO.Path]::GetFullPath($PackageDirectory)
$output=[IO.Path]::GetFullPath($OutputDirectory)
$msi=Join-Path $output "CompositorWindows-$Version-x64.msi"
if(Test-Path -LiteralPath $msi){throw "Installer already exists: $msi"}
if(-not(Test-Path -LiteralPath (Join-Path $package 'Compositor.exe'))){throw 'The flat application payload is missing'}
$work=Join-Path $output ('msi-build-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $work | Out-Null
$toolchain=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw | ConvertFrom-Json
$toolRoot=Join-Path $root 'dependencies\packaging'
$compiler=Join-Path (Join-Path $toolRoot $toolchain.assets[0].directory) $toolchain.assets[0].entry
$extension=Join-Path (Join-Path $toolRoot $toolchain.assets[1].directory) $toolchain.assets[1].entry
$xml=[Xml.XmlDocument]::new()
$wix=$xml.CreateElement('Wix','http://wixtoolset.org/schemas/v4/wxs'); [void]$xml.AppendChild($wix)
function Add-Element($parent,[string]$name,[hashtable]$attributes){
    $element=$xml.CreateElement($name,$wix.NamespaceURI)
    foreach($key in $attributes.Keys){$element.SetAttribute($key,[string]$attributes[$key])}
    [void]$parent.AppendChild($element); return $element
}
function Identifier([string]$prefix,[string]$path){
    $bytes=[Text.Encoding]::UTF8.GetBytes($path.Replace('\','/').ToLowerInvariant())
    return $prefix+[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).Substring(0,32)
}
function Component-Guid([string]$path){
    $hash=[Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes('CompositorWindows/CommunityPreview/'+$path.ToLowerInvariant()))
    $guidBytes=[byte[]]$hash[0..15]
    $guidBytes[7]=($guidBytes[7] -band 0x0f) -bor 0x50
    $guidBytes[8]=($guidBytes[8] -band 0x3f) -bor 0x80
    return [Guid]::new($guidBytes).ToString()
}
$fragment=Add-Element $wix 'Fragment' @{}
$group=Add-Element $fragment 'ComponentGroup' @{Id='ApplicationFiles'}
$directoryRef=Add-Element $fragment 'DirectoryRef' @{Id='INSTALLFOLDER'}
function Add-Directory($element,[string]$path,[string]$relative){
    $folderComponent=Add-Element $element 'Component' @{Id=(Identifier 'D' $relative);Guid='*'}
    [void](Add-Element $folderComponent 'RegistryValue' @{Root='HKCU';Key='Software\CompositorWindows\CommunityPreview\Folders';Name=(Identifier 'D' $relative);Type='integer';Value='1';KeyPath='yes'})
    [void](Add-Element $folderComponent 'RemoveFolder' @{Id=(Identifier 'R' $relative);On='uninstall'})
    [void](Add-Element $group 'ComponentRef' @{Id=$folderComponent.GetAttribute('Id')})
    foreach($entry in Get-ChildItem -LiteralPath $path -Force | Sort-Object Name){
        if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Package reparse point is not allowed: $($entry.FullName)"}
        $childRelative=if($relative){$relative+'/'+$entry.Name}else{$entry.Name}
        if($entry.PSIsContainer){
            $child=Add-Element $element 'Directory' @{Id=(Identifier 'F' $childRelative);Name=$entry.Name}
            Add-Directory $child $entry.FullName $childRelative
        }else{
            $id=Identifier 'C' $childRelative
            $component=Add-Element $element 'Component' @{Id=$id;Guid=(Component-Guid $childRelative)}
            [void](Add-Element $component 'File' @{Id=(Identifier 'A' $childRelative);Source=$entry.FullName;Name=$entry.Name})
            [void](Add-Element $component 'RegistryValue' @{Root='HKCU';Key='Software\CompositorWindows\CommunityPreview\Files';Name=$id;Type='integer';Value='1';KeyPath='yes'})
            [void](Add-Element $group 'ComponentRef' @{Id=$id})
        }
    }
}
Add-Directory $directoryRef $package ''
$payloadSource=Join-Path $work 'Payload.wxs'; $xml.Save($payloadSource)
$license="Compositor Windows Community Preview`r`n`r`nCommunity port of Compositor by Robbie Tilton, https://github.com/robbietilton/Compositor, baseline a19db9011282399785dc18efcfded904627bdcc2 (1.0.4). This port is not endorsed by the upstream author.`r`n`r`n"+(Get-Content -LiteralPath (Join-Path $root 'LICENSE') -Raw)
$rtf='{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\f0\fs20 '+$license.Replace('\','\\').Replace('{','\{').Replace('}','\}').Replace("`r`n",'\par ').Replace("`n",'\par ')+'}'
$licensePath=Join-Path $work 'License.rtf'; [IO.File]::WriteAllText($licensePath,$rtf,[Text.Encoding]::ASCII)
& dotnet exec --roll-forward Major $compiler build -arch x64 -ext $extension -culture en-us -wx -ct 2 -pdbtype none `
    -d "Version=$Version" -d "Icon=$(Join-Path $root 'assets\app.ico')" -d "LicenseRtf=$licensePath" `
    -intermediatefolder $work -o $msi (Join-Path $PSScriptRoot 'Package.wxs') $payloadSource *> (Join-Path $output 'installer-build.log')
if($LASTEXITCODE){throw "MSI compilation failed; see $output\installer-build.log"}
# ICE91 warns about intentionally per-user payloads; all other ICEs remain enabled.
& dotnet exec --roll-forward Major $compiler msi validate $msi -sice ICE91 -wx *> (Join-Path $output 'installer-validation.log')
if($LASTEXITCODE){throw "MSI validation failed; see $output\installer-validation.log"}
Write-Output "MSI=$msi"
