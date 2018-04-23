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
	public partial class BasicChannels : ContentPage
	{
    IYse.ISound ambient;
    IYse.ISound music;
    IYse.ISound voice;

		public BasicChannels ()
		{
			InitializeComponent ();

      Info.Text = AppResources.BasicsChannelsInfo;

      ambient = Global.Yse.NewSound();
      ambient.Create("flies", Global.Yse.ChannelAmbient, true);
      ambient.Play();

      music = Global.Yse.NewSound();
      music.Create("my2chords", Global.Yse.ChannelMusic, true);
      music.Play();

      voice = Global.Yse.NewSound();
      voice.Create("countdown", Global.Yse.ChannelVoice, true);
      voice.Play();
		}

    protected override void OnDisappearing()
    {
      // reset channel volumes
      Global.Yse.ChannelMaster.Volume = 1f;
      Global.Yse.ChannelMusic.Volume = 1f;
      Global.Yse.ChannelAmbient.Volume = 1f;
      Global.Yse.ChannelVoice.Volume = 1f;

      // dispose of sound objects
      ambient.Dispose();
      music.Dispose();
      voice.Dispose();
      base.OnDisappearing();
    }

    private void AmbientChanged(object sender, EventArgs e)
    {
      Global.Yse.ChannelAmbient.Volume = (float)(sender as Slider).Value;
    }

    private void MusicChanged(object sender, EventArgs e)
    {
      Global.Yse.ChannelMusic.Volume = (float)(sender as Slider).Value;
    }

    private void VoiceChanged(object sender, EventArgs e)
    {
      Global.Yse.ChannelVoice.Volume = (float)(sender as Slider).Value;
    }

    private void MasterChanged(object sender, EventArgs e)
    {
      Global.Yse.ChannelMaster.Volume = (float)(sender as Slider).Value;
    }
  }
}