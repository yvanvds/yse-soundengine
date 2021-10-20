using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace GeneratePackage
{
    class Program
    {
        static void Main(string[] args)
        {
            string baseDir = Directory.GetCurrentDirectory();
            baseDir = baseDir.Replace("GeneratePackage\\bin\\Debug", "");
            Console.WriteLine("Starting in: " + baseDir);

            
            string packageFolder = Path.Combine(baseDir, "YseCppRelease");
            if (Directory.Exists(packageFolder))
            {
                Console.WriteLine("Remove Old Package Folder");
                Directory.Delete(packageFolder, true);
            }

            Directory.CreateDirectory(packageFolder);
            string includeFolder = Path.Combine(packageFolder, "include");
            Directory.CreateDirectory(includeFolder);

            string sourceFolder = Path.Combine(baseDir, "YseEngine");
            if (!Directory.Exists(sourceFolder))
            {
                Console.WriteLine("Source Not found at " + sourceFolder);
                Console.ReadKey();
                return;
            }

            Console.WriteLine("Copying include files ...");
            CopyHeadersRecursive(sourceFolder, includeFolder);

            Console.WriteLine("Copying library files ...");
            string lib32Folder = Path.Combine(packageFolder, "x32");
            Directory.CreateDirectory(lib32Folder);
            File.Copy(Path.Combine(baseDir, "Yse.Windows.Native/Release/Win32/Yse.lib"), Path.Combine(lib32Folder, "Yse.lib"));

            Console.WriteLine("Copying dll files ...");
            File.Copy(Path.Combine(baseDir, "Yse.Windows.Native/Release/Win32/Yse.dll"), Path.Combine(lib32Folder, "Yse.dll"));
            File.Copy(Path.Combine(baseDir, "dependencies/libsndfile/bin/libsndfile-1.dll"), Path.Combine(lib32Folder, "libsndfile-1.dll"));
            File.Copy(Path.Combine(baseDir, "dependencies/portaudio/lib/releasedll/portaudio_x86.dll"), Path.Combine(lib32Folder, "portaudio_x86.dll"));

            Console.WriteLine("Copying 64-bit library files ...");
            string lib64Folder = Path.Combine(packageFolder, "x64");
            Directory.CreateDirectory(lib64Folder);
            File.Copy(Path.Combine(baseDir, "Yse.Windows.Native/Release/x64/Yse.lib"), Path.Combine(lib64Folder, "Yse.lib"));

            Console.WriteLine("Copying 64-bit dll files ...");
             File.Copy(Path.Combine(baseDir, "Yse.Windows.Native/Release/x64/Yse.dll"), Path.Combine(lib64Folder, "Yse.dll"));
            File.Copy(Path.Combine(baseDir, "dependencies/libsndfile64/bin/libsndfile-1.dll"), Path.Combine(lib64Folder, "libsndfile-1.dll"));
            File.Copy(Path.Combine(baseDir, "dependencies/portaudio/lib/releasedll/portaudio_x64.dll"), Path.Combine(lib64Folder, "portaudio_x64.dll"));


            Console.WriteLine("Done!");
            Console.ReadKey();
        }

        static void CopyHeadersRecursive(string source, string target)
        {
            foreach(var file in Directory.GetFiles(source))
            {
                if (file.EndsWith(".hpp"))
                {
                    File.Copy(file, Path.Combine(target, Path.GetFileName(file)));
                }
            }

            foreach(var directory in Directory.GetDirectories(source))
            {
                string lastPart = Path.GetFileName(directory);
                string newTarget = Path.Combine(target, lastPart);
                Directory.CreateDirectory(newTarget);
                CopyHeadersRecursive(Path.Combine(source, lastPart), newTarget);
            }
        }
    }
}
