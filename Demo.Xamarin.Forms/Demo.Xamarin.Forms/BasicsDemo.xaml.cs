using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Xamarin.Forms;
using Xamarin.Forms.Xaml;

namespace Demo.Xamarin.Forms
{
	[XamlCompilation(XamlCompilationOptions.Compile)]
	public partial class BasicsDemo : ContentPage
	{
		public BasicsDemo ()
		{
			InitializeComponent ();
      
		}

    private void AudioTest_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new Demos.BasicAudioTest());
    }

    private void PlaySound_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new Demos.BasicPlaySound());
    }

    private void Demo3D_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new Demos.Basic3D());
    }

    private void Channels_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new Demos.BasicChannels());
    }

    private void Reverb_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new Demos.BasicReverb());
    }
  }
}