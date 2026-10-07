package app.organicmaps.sdk.commuter_live;

/** One live FGC or Rodalies train. Position comes from the operator feed. */
public final class CommuterTrain
{
  public String mLine;
  public String mColor;
  public String mDestination;
  public String mNextStop;
  public String mKey;
  public double mLat;
  public double mLon;
  /** Clockwise degrees from north. Used only when {@link #mDirectional} is true. */
  public double mHeadingDeg;
  public boolean mDirectional;
}
