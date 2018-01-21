using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Windows.Threading;
using Yse;

namespace Demo.Windows.WPF
{
  /// <summary>
  /// Interaction logic for MainWindow.xaml
  /// </summary>
  public partial class MainWindow : Window
  {
    Yse.sound sound = null;

    sound patcherSound = null;
    patcher patcher = null;

    DispatcherTimer timer = new DispatcherTimer();
    bool AudioTestActive = false;

    public MainWindow()
    {
      InitializeComponent();
      Yse.Yse.System().init();

      timer.Tick += new EventHandler(Update);
      timer.Interval = new TimeSpan(0, 0, 0, 0, 5);
      timer.Start();
    }

    private void Update(object sender, EventArgs e)
    {
      Yse.Yse.System().update();
    }

    protected override void OnClosed(EventArgs e)
    {
      base.OnClosed(e);
      timer.Stop();
      Yse.Yse.System().close();
    }

    private void FileButton_Click(object sender, RoutedEventArgs e)
    {
      OpenFileDialog openFileDialog = new OpenFileDialog();
      if (openFileDialog.ShowDialog() == true)
      {
        if (sound != null) sound.Dispose();
        sound = new sound();
        sound.create(openFileDialog.FileName);
      }
    }

    private void PlayButton_Click(object sender, RoutedEventArgs e)
    {
      if (sound != null && sound.isValid()) sound.play();
    }

    private void PauseButton_Click(object sender, RoutedEventArgs e)
    {
      if (sound != null) sound.pause();
    }

    private void StopButton_Click(object sender, RoutedEventArgs e)
    {
      if (sound != null) sound.stop();
    }

    private void TestButton_Click(object sender, RoutedEventArgs e)
    {
      AudioTestActive = !AudioTestActive;
      Yse.Yse.System().AudioTest(AudioTestActive);
    }

    private void PatcherButton_Click(object sender, RoutedEventArgs e)
    {
      if(patcherSound == null)
      {
        patcherSound = new sound();
        patcher = new patcher();
        patcher.create(1);
        pHandle handle = patcher.CreateObject("~sine");
        patcher.Connect(handle, 0, patcher.GetOutputHandle(0), 0);
        patcherSound.create(patcher);
        patcherSound.play();
      }
    }
  }
}
