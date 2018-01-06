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
    Dictionary<string, YSE.REVERB_PRESET> stringToPreset = new Dictionary<string, YSE.REVERB_PRESET>
    {
      {"Off",YSE.REVERB_PRESET.REVERB_OFF },
      {"Generic",YSE.REVERB_PRESET.REVERB_GENERIC },
      {"Padded",YSE.REVERB_PRESET.REVERB_PADDED },
      {"Room",YSE.REVERB_PRESET.REVERB_ROOM },
      {"Bathroom",YSE.REVERB_PRESET.REVERB_BATHROOM },
      {"Stone Room", YSE.REVERB_PRESET.REVERB_STONEROOM },
      {"Large Room",YSE.REVERB_PRESET.REVERB_LARGEROOM },
      {"Hall",YSE.REVERB_PRESET.REVERB_HALL },
      {"Cave",YSE.REVERB_PRESET.REVERB_CAVE },
      {"Sewerpipe",YSE.REVERB_PRESET.REVERB_SEWERPIPE },
      {"Underwater",YSE.REVERB_PRESET.REVERB_UNDERWATER }
    };

    YSE.IReverb reverb = Global.Yse.System.GetReverb();
    YSE.ISound snare;

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

      snare = Global.Yse.CreateSound();
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
        reverb.SetPreset(YSE.REVERB_PRESET.REVERB_OFF);
      } else
      {
        reverb.SetPreset(stringToPreset[ReverbChoice.Items[ReverbChoice.SelectedIndex]]);
      }
    }
	}
}