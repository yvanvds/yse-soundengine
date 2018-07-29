using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Xamarin.Forms;
using Xamarin.Forms.Xaml;
using SkiaSharp;
using SkiaSharp.Views.Forms;
using IYse;

namespace Demo.Xamarin.Forms.Demos
{
	[XamlCompilation(XamlCompilationOptions.Compile)]
	public partial class DSPSimplePatcher : ContentPage
	{
    IHandle mtof;
    IHandle sine;
    IHandle lfo;
    IHandle volume;
    IHandle dac;

		IHandle pitchIn;
		IHandle volumeIn;
		IHandle lfoIn;

    ISound sound;
    IPatcher patcher;

    SKPaint bluePaint = new SKPaint
    {
      Style = SKPaintStyle.Fill,
      Color = SKColors.Blue
    };

    SKPoint size = new SKPoint();
    SKPoint touch = new SKPoint();
    bool On = false;

    public DSPSimplePatcher ()
		{
			InitializeComponent ();

      sound = Global.Yse.NewSound();
      patcher = Global.Yse.NewPatcher();
      patcher.Create(1);

      mtof = patcher.CreateObject(".mtof");
      sine = patcher.CreateObject("~sine");
      lfo = patcher.CreateObject("~sine");
      volume = patcher.CreateObject("~*");
      dac = patcher.CreateObject("~dac");

			pitchIn = patcher.CreateObject(".r", "pitch");
			volumeIn = patcher.CreateObject(".r", "volume");
			lfoIn = patcher.CreateObject(".r", "lfo");

      IHandle multiplier = patcher.CreateObject("~*");

      patcher.Connect(mtof, 0, sine, 0); // pass frequency to sine
      patcher.Connect(sine, 0, multiplier, 0);
      patcher.Connect(lfo, 0, multiplier, 1);
      patcher.Connect(multiplier, 0, volume, 0);
      patcher.Connect(volume, 0, dac, 0);
			patcher.Connect(pitchIn, 0, mtof, 0);
			patcher.Connect(volumeIn, 0, volume, 1);
			patcher.Connect(lfoIn, 0, lfo, 0);

			patcher.PassData(60F, "pitch");
			patcher.PassData(0F, "volume");
			patcher.PassData(4F, "lfo");

      //mtof.SetFloatData(0, 60f);
      //lfo.SetFloatData(0, 4f);
      //volume.SetFloatData(1, 0f);

      sound.Create(patcher);
      sound.Play();
		}

    protected override void OnDisappearing()
    {
      patcher.Dispose();
      sound.Dispose();
      base.OnDisappearing();
    }

    private void PaintSurface(object sender, SKPaintSurfaceEventArgs e)
    {
      SKSurface surface = e.Surface;
      SKCanvas canvas = surface.Canvas;

      canvas.Clear(SKColors.DimGray);

      size.X = e.Info.Width;
      size.Y = e.Info.Height;

      if(On)
      {
        canvas.DrawCircle(touch.X, touch.Y, 80, bluePaint);
      }
    }

    private void OnTouch(object sender, SKTouchEventArgs e)
    {
      switch(e.ActionType)
      {
        case SKTouchAction.Pressed:
          On = true;
					//volume.SetFloatData(1, 1f);
					patcher.PassData(1f, "volume");
          touch = e.Location; 
          e.Handled = true;
          break;

        case SKTouchAction.Moved:
          touch = e.Location;
          if (size.X == 0f || size.Y == 0f) return;

					patcher.PassData(60 + (int)(touch.Y / size.Y * 40f), "pitch");

					patcher.PassData(touch.X / size.X * 10f, "lfo");

          e.Handled = true;
          break;

        case SKTouchAction.Released:
          On = false;
          //volume.SetFloatData(1, 0f);
					patcher.PassData(0f, "volume");
					touch = e.Location;
          e.Handled = true;
          break;
      }

      canvasView.InvalidateSurface();
    }
  }
}