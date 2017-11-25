

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class SgfUtils {
	
	class SgfThread implements Runnable {
		private String srcPath;
		private String dstPath;
		CountDownLatch downLatch;
		
		public SgfThread(String srcPath, String dstPath, CountDownLatch downLatch) {
			this.srcPath = srcPath;
			this.dstPath = dstPath;
			this.downLatch = downLatch;
		}
		
		public void run() {
			try {
				ProcessBuilder processBuilder = 
		        		new ProcessBuilder("./sgfutils.sh", this.srcPath, this.dstPath);
		        processBuilder.directory(
		        		new File("/app"));
		        
		        Process process = processBuilder.start();
		        InputStream is = process.getInputStream();
		        InputStreamReader isr = new InputStreamReader(is);
		        
		        BufferedReader br = new BufferedReader(isr);
		        String tmpLine = null;
		        StringBuilder strBuilder = new StringBuilder();
		        int i =0;
		        while((tmpLine=br.readLine())!=null) {
		            if(i>0) {
		                strBuilder.append("\n");
		            }
		            strBuilder.append(tmpLine);
		            i++;
		        }
		        
		        br.close();
		        isr.close();
		        is.close();
		        process.destroy();
		        processBuilder = null;
		        System.out.println(strBuilder.toString());
			} catch (Exception e) {
				e.printStackTrace();
			} finally {
				this.downLatch.countDown();
			}
		}
	}
	
	public void convert(String srcPath, String savePath, int threadCount) throws Exception {
		File srcFolder = new File(srcPath);
		File[] files = srcFolder.listFiles();
		ExecutorService executor = Executors.newFixedThreadPool(threadCount);
		CountDownLatch downLatch = new CountDownLatch(files.length);
		System.out.println("file count :" + files.length);
		for(File tmpFile : files) {
			executor.submit(new SgfThread(tmpFile.getAbsolutePath(), savePath, downLatch));
		}
		
		downLatch.await();
		executor.shutdown();
	}

	public static void main(String[] args) throws Exception {
		if(args == null
			|| args.length != 3) {
			System.out.println("usage : SgfUtils ${source_path} ${save_path} ${thread_count}");
			System.exit(1);
		}
		
		File srcFolder = new File(args[0]);
		if(!(srcFolder.exists() && srcFolder.isDirectory())) {
			System.out.println("Please check the source folder path is valid!");
			System.exit(1);
		}
		
		File saveFolder = new File(args[1]);
		if(!(saveFolder.exists() && saveFolder.isDirectory())) {
			System.out.println("Please check the save folder path is valid !");
			System.exit(1);
		}
		
		int threadCount = Integer.parseInt(args[2]);
		SgfUtils instance = new SgfUtils();
		instance.convert(srcFolder.getAbsolutePath(), saveFolder.getAbsolutePath(), threadCount);
	}

}
