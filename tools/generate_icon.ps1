# Regenerate the original eye + clock icon without external dependencies.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Collections.Generic;

public static class RestCueIconGenerator {
    static Bitmap Draw(int size) {
        const int factor = 4;
        using (var large = new Bitmap(size * factor, size * factor, PixelFormat.Format32bppArgb)) {
            using (var g = Graphics.FromImage(large)) {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.Clear(Color.Transparent);
                g.ScaleTransform(size * factor / 256f, size * factor / 256f);
                using (var rounded = new GraphicsPath()) {
                    rounded.AddArc(8, 8, 104, 104, 180, 90);
                    rounded.AddArc(144, 8, 104, 104, 270, 90);
                    rounded.AddArc(144, 144, 104, 104, 0, 90);
                    rounded.AddArc(8, 144, 104, 104, 90, 90);
                    rounded.CloseFigure();
                    using (var bg = new LinearGradientBrush(new Point(8, 8), new Point(248, 248),
                        Color.FromArgb(26, 175, 161), Color.FromArgb(19, 86, 111))) {
                        g.FillPath(bg, rounded);
                    }
                }
                using (var eye = new GraphicsPath())
                using (var white = new SolidBrush(Color.FromArgb(245, 255, 252)))
                using (var pupil = new SolidBrush(Color.FromArgb(22, 103, 122)))
                using (var glint = new SolidBrush(Color.FromArgb(164, 238, 224)))
                using (var rim = new Pen(Color.FromArgb(22, 77, 102), 8))
                using (var hands = new Pen(Color.FromArgb(22, 123, 131), 10)) {
                    eye.AddBezier(44, 114, 80, 67, 141, 67, 178, 114);
                    eye.AddBezier(178, 114, 141, 161, 80, 161, 44, 114);
                    eye.CloseFigure();
                    g.FillPath(white, eye);
                    g.FillEllipse(pupil, 86, 89, 50, 50);
                    g.FillEllipse(glint, 97, 99, 14, 14);
                    g.FillEllipse(white, 137, 137, 96, 96);
                    g.DrawEllipse(rim, 137, 137, 96, 96);
                    hands.StartCap = LineCap.Round;
                    hands.EndCap = LineCap.Round;
                    hands.LineJoin = LineJoin.Round;
                    g.DrawLines(hands, new Point[] {
                        new Point(185, 158), new Point(185, 185), new Point(204, 196)
                    });
                }
            }
            var result = new Bitmap(size, size, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(result)) {
                g.CompositingMode = CompositingMode.SourceCopy;
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.DrawImage(large, new Rectangle(0, 0, size, size));
            }
            return result;
        }
    }

    public static void Generate(string directory) {
        Directory.CreateDirectory(directory);
        int[] sizes = { 16, 24, 32, 48, 64, 128, 256 };
        var images = new List<byte[]>();
        foreach (int size in sizes) {
            using (var bmp = Draw(size))
            using (var data = new MemoryStream()) {
                bmp.Save(data, ImageFormat.Png);
                images.Add(data.ToArray());
                if (size == 256) bmp.Save(Path.Combine(directory, "restcue.png"), ImageFormat.Png);
            }
        }
        using (var writer = new BinaryWriter(File.Create(Path.Combine(directory, "restcue.ico")))) {
            writer.Write((ushort)0);
            writer.Write((ushort)1);
            writer.Write((ushort)sizes.Length);
            uint offset = (uint)(6 + 16 * sizes.Length);
            for (int i = 0; i < sizes.Length; i++) {
                writer.Write((byte)(sizes[i] == 256 ? 0 : sizes[i]));
                writer.Write((byte)(sizes[i] == 256 ? 0 : sizes[i]));
                writer.Write((byte)0);
                writer.Write((byte)0);
                writer.Write((ushort)1);
                writer.Write((ushort)32);
                writer.Write((uint)images[i].Length);
                writer.Write(offset);
                offset += (uint)images[i].Length;
            }
            foreach (var data in images) writer.Write(data);
        }
    }
}
'@
$assets = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\assets'))
[RestCueIconGenerator]::Generate($assets)
Write-Output "Generated: $assets\restcue.ico (7 sizes) and restcue.png"
