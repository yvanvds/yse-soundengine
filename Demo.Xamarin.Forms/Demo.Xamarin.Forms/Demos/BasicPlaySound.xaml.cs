using Demo.Xamarin.Forms.Resources;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Xamarin.Forms;
using Xamarin.Forms.Xaml;

namespace Demo.Xamarin.Forms.Demos
{
	[XamlCompilation(XamlCompilationOptions.Compile)]
	public partial class BasicPlaySound : ContentPage
	{
    IYse.ISound sound;
    bool playing = false;

		public BasicPlaySound ()
		{
			InitializeComponent ();

      Info.Text = AppResources.BasicsPlaySoundInfo;

      
		}

    private void ButtonPlay(object sender, EventArgs e)
    {
      sound.Play();
      playing = true;
    }

    private void ButtonPause(object sender, EventArgs e)
    {
      sound.Pause();
      playing = false;
    }

    private void ButtonStop(object sender, EventArgs e)
    {
      sound.Stop();
      playing = false;
    }

    private void VolumeChanged(object sender, EventArgs e)
    {
      sound.Volume = (float)(sender as Slider).Value;
    }

    private void SpeedChanged(object sender, EventArgs e)
    {
      sound.Speed = (float)(sender as Slider).Value;
    }

    protected override void OnDisappearing()
    {
      sound.Stop();
      sound.Dispose();
      base.OnDisappearing();
    }

    protected override void OnAppearing()
    {
      base.OnAppearing();
      sound = Global.Yse.NewSound();
      sound.Create("snare", Global.Yse.ChannelMaster, true);
      sound.Volume = 0.5f;

      if (playing) sound.Play();
      sound.Volume = (float)VolumeSlider.Value;
      sound.Speed = (float)SpeedSlider.Value;
    }
  }
}