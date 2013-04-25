package j3.vm;

public class OSGi
{
	// OSGi hooks and information gathering
	public static native void associateBundleClass(long bundleID, Class classObject);
    public static native void notifyBundleUninstalled(long bundleID);
    
    public static native long[] getReferencesToObject(long objectPointer);
    public static native String dumpObject(long objectPointer);
	
	// Commands
    public static native void setBundleStaleReferenceCorrected(long bundleID, boolean corrected);
    public static native boolean isBundleStaleReferenceCorrected(long bundleID);
	public static native void dumpClassLoaderBundles();
}
