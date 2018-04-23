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
	public partial class BasicReverb : ContentPage
	{
    Dictionary<string, IYse.REVERB_PRESET> stringToPreset = new Dictionary<string, IYse.REVERB_PRESET>
    {
      {"Off",IYse.REVERB_PRESET.REVERB_OFF },
      {"Generic",IYse.REVERB_PRESET.REVERB_GENERIC },
      {"Padded",IYse.REVERB_PRESET.REVERB_PADDED },
      {"Room",IYse.REVERB_PRESET.REVERB_ROOM },
      {"Bathroom",IYse.REVERB_PRESET.REVERB_BATHROOM },
      {"Stone Room", IYse.REVERB_PRESET.REVERB_STONEROOM },
      {"Large Room",IYse.REVERB_PRESET.REVERB_LARGEROOM },
      {"Hall",IYse.REVERB_PRESET.REVERB_HALL },
      {"Cave",IYse.REVERB_PRESET.REVERB_CAVE },
      {"Sewerpipe",IYse.REVERB_PRESET.REVERB_SEWERPIPE },
      {"Underwater",IYse.REVERB_PRESET.REVERB_UNDERWATER }
    };

    IYse.IReverb reverb = Global.Yse.System.GetReverb();
    IYse.ISound snare;

		public BasicReverb ()
		{
			InitializeComponent ();
      Info.Text = AppResources.BasicsReverbInfo;

      foreach(string name in stringToPreset.Keys)
      {
        ReverbChoice.Items.Add(name);
      }

      reverb.Active = true;
      Global.Yse.ChannelMaster.AttachReverb();

      snare = Global.Yse.NewSound();
      snare.Create("snare", null, true);
      snare.Play();
		}

    protected override void OnDisappearing()
    {
      snare.Stop();
      reverb.Active = false;
      snare.Dispose();
      base.OnDisappearing();
    }

    private void OnPickReverb(object sender, EventArgs e)
    {
      if(ReverbChoice.SelectedIndex == -1)
      {
        reverb.SetPreset(IYse.REVERB_PRESET.REVERB_OFF);
      } else
      {
        reverb.SetPreset(stringToPreset[ReverbChoice.Items[ReverbChoice.SelectedIndex]]);
      }
    }
	}
}